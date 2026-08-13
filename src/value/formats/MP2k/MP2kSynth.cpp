/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MP2k/MP2k.h"

#include "value/formats/MP2k/MP2kEnvelope.h"

#include "value/base/RecordReader.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::mp2k {

using namespace core;

namespace {

constexpr double kPsgSampleFrequency = 440.0;
constexpr u32 kPsgLoopGuardSamples = 8;
constexpr u64 kProgrammableWaveKeyBase = u64{1} << 32;
// Aria routes the summed CGB envelope through one hardware-volume lane while
// DirectSound mixes independent left and right lanes. With both GBA output
// ratios at full scale, the CGB path is therefore one half of DirectSound.
constexpr double kPsgDacAttenuationDb = 6.020599913279624;
constexpr u8 kToneCgbMask = 0x07;
constexpr u8 kToneFixed = 0x08;
constexpr u8 kToneReverse = 0x10;
constexpr u8 kToneSplit = 0x40;
constexpr u8 kToneRhythm = 0x80;
constexpr std::array<u8, 60> kNoiseRegisters{
    0xd7, 0xd6, 0xd5, 0xd4, 0xc7, 0xc6, 0xc5, 0xc4, 0xb7, 0xb6, 0xb5, 0xb4, 0xa7, 0xa6, 0xa5,
    0xa4, 0x97, 0x96, 0x95, 0x94, 0x87, 0x86, 0x85, 0x84, 0x77, 0x76, 0x75, 0x74, 0x67, 0x66,
    0x65, 0x64, 0x57, 0x56, 0x55, 0x54, 0x47, 0x46, 0x45, 0x44, 0x37, 0x36, 0x35, 0x34, 0x27,
    0x26, 0x25, 0x24, 0x17, 0x16, 0x15, 0x14, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
};
constexpr std::array<s16, 12> kCgbFrequencyTable{
    -2004, -1891, -1785, -1685, -1591, -1501, -1417, -1337, -1262, -1192, -1125, -1062,
};

struct Tone {
  u8 type = 0;
  u8 key = 60;
  u8 length = 0;
  u8 panSweep = 0;
  u32 wave = 0;
  u8 attack = 0;
  u8 decay = 0;
  u8 sustain = 0;
  u8 release = 0;
  SourceRange range;
  SourceRecord source;

  [[nodiscard]] u8 cgbType() const { return type & kToneCgbMask; }
  [[nodiscard]] bool fixed() const { return (type & kToneFixed) != 0; }
  [[nodiscard]] bool reverse() const { return (type & kToneReverse) != 0; }
  [[nodiscard]] bool split() const { return (type & kToneSplit) != 0; }
  [[nodiscard]] bool rhythm() const { return (type & kToneRhythm) != 0; }
  [[nodiscard]] bool table() const { return split() || rhythm(); }
};

struct SynthContext {
  ScanResultBuilder& builder;
  u32 sampleRate = 0;
  u8 directSoundMasterVolume = 15;
  u8 dacBits = 8;
  ScanSampleCollectionDraft& psg;
  std::optional<ScanSampleCollectionDraft>& pcm;
  u32 bankOffset = 0;

  [[nodiscard]] ScanSampleCollectionDraft& pcmSamples() {
    if (!pcm) {
      pcm.emplace(builder.sampleCollection(fmt::format("MP2k samples {:#x}", bankOffset)));
    }
    return *pcm;
  }
};

[[nodiscard]] std::optional<u32> romOffset(u32 address, ByteReader reader, u32 size = 1) {
  if ((address & 0xfe000000) != 0x08000000) {
    return std::nullopt;
  }
  const u32 offset = address & 0x01ffffff;
  return reader.has(offset, size) ? std::optional<u32>{offset} : std::nullopt;
}

[[nodiscard]] std::optional<Tone> readTone(ScanResultBuilder& builder, u32 offset) {
  if (!builder.reader().has(offset, 12)) {
    return std::nullopt;
  }
  RecordReader record(builder.reader(), offset, offset + 12, &builder.diagnostics());
  const auto type = record.u8("type", SourceValueDisplay::Hex);
  const auto key = record.u8("key", SourceValueDisplay::MidiNote);
  const auto length = record.u8("length");
  const auto panSweep = record.u8("pan_sweep", SourceValueDisplay::Hex);
  const auto wave = record.u32le("wave", SourceValueDisplay::Address);
  const auto attack = record.u8("attack");
  const auto decay = record.u8("decay");
  const auto sustain = record.u8("sustain");
  const auto release = record.u8("release");
  if (!record.ok()) {
    return std::nullopt;
  }
  Tone tone{
      .type = *type,
      .key = *key,
      .length = *length,
      .panSweep = *panSweep,
      .wave = *wave,
      .attack = *attack,
      .decay = *decay,
      .sustain = *sustain,
      .release = *release,
      .range = builder.reader().range(offset, 12),
  };
  tone.source = std::move(record).finish();
  return tone;
}

[[nodiscard]] Envelope envelopeFor(const Tone& tone, bool cgb) {
  return Envelope{
      .attackSeconds = cgb ? cgbEnvelopeSeconds(tone.attack) : directAttackSeconds(tone.attack),
      // DirectSound changes ATK to DEC on the frame that reaches 0xff; decay
      // is not evaluated until the following SoundMainRAM pass.
      .holdSeconds = cgb ? std::optional<double>{} : std::optional{1.0 / kGbaMixerFrameRate},
      .decaySeconds = cgb ? cgbEnvelopeSeconds(tone.decay) : directDecaySeconds(tone.decay),
      .releaseSeconds = cgb ? cgbEnvelopeSeconds(tone.release) : directReleaseSeconds(tone.release),
      .sustainAmplitude = cgb ? std::min<u8>(tone.sustain, 15) / 15.0 : tone.sustain / 255.0,
  };
}

[[nodiscard]] double directSoundMasterAttenuation(u8 volume) {
  // The software mixer scales its 8-bit envelope by (masterVolume + 1) / 16
  // before applying the channel's linear left/right volume bytes.
  return -20.0 * std::log10((volume + 1.0) / 16.0);
}

[[nodiscard]] double noiseClockHertz(u8 key) {
  const u8 index = key <= 20 ? 0 : std::min<u8>(key - 21, 59);
  const u8 reg = kNoiseRegisters[index];
  const double divisor = (reg & 7) == 0 ? 0.5 : reg & 7;
  return 524288.0 / divisor / std::exp2((reg >> 4) + 1.0);
}

[[nodiscard]] s32 arithmeticShiftRight(s32 value, u32 bits) {
  if (value >= 0 || bits == 0) {
    return value >> bits;
  }
  return -static_cast<s32>((static_cast<u32>(-value) + (u32{1} << bits) - 1) >> bits);
}

[[nodiscard]] u16 cgbFrequencyRegister(u8 key, bool fixed, u8 dacBits) {
  const u8 index = key <= 35 ? 0 : std::min<u8>(key - 36, 130);
  const u32 octave = index / 12;
  u16 frequency = static_cast<u16>(2048 + arithmeticShiftRight(kCgbFrequencyTable[index % 12], octave));
  if (fixed && dacBits >= 9) {
    frequency = static_cast<u16>((frequency + 2) & 0x7fc);
  } else if (fixed && dacBits == 8) {
    frequency = static_cast<u16>((frequency + 1) & 0x7fe);
  }
  return frequency;
}

[[nodiscard]] double cgbClockHertz(u8 channel, u8 key, bool fixed, u8 dacBits) {
  const double numerator = channel == 3 ? 65536.0 : 131072.0;
  return numerator / (2048 - cgbFrequencyRegister(key, fixed, dacBits));
}

[[nodiscard]] InstrumentModulation mp2kModulation() {
  const double maximumRate = 127.0 * kGbaMixerFrameRate / 256.0;
  return InstrumentModulation{
      .vibrato =
          VibratoSpec{
              .maxDepthCents = 127.0 * 100.0 / 16.0,
              .rateHertz = {.minimum = 0.0, .maximum = maximumRate},
              .waveform = LfoWaveform::Triangle,
              .delaySeconds = ModulationRange{.minimum = 0.0, .maximum = 255.0 / kGbaMixerFrameRate},
          },
      .tremolo =
          TremoloSpec{
              .maxDepthDb = 20.0 * std::log10(1.0 + 127.0 / 128.0),
              .rateHertz = {.minimum = 0.0, .maximum = maximumRate},
              .waveform = LfoWaveform::Triangle,
              .gainMode = TremoloGainMode::BipolarAroundNominal,
              .delaySeconds = ModulationRange{.minimum = 0.0, .maximum = 255.0 / kGbaMixerFrameRate},
          },
  };
}

[[nodiscard]] std::optional<SampleRef> addPcmSample(SynthContext& context, u32 pointer, bool reverse,
                                                    double& unityKey) {
  auto& builder = context.builder;
  const auto offset = romOffset(pointer, builder.reader(), 16);
  if (!offset) {
    return std::nullopt;
  }
  const u64 sampleKey = reverse ? (u64{1} << 63) | *offset : *offset;
  if (context.pcm) {
    if (const auto existing = context.pcm->find(sampleKey)) {
      const u32 frequency = builder.reader().le32(*offset + 4);
      unityKey = frequency == 0 ? 60.0 : 60.0 + 12.0 * std::log2(context.sampleRate * 1024.0 / frequency);
      return existing;
    }
  }

  RecordReader header(builder.reader(), *offset, *offset + 16, &builder.diagnostics());
  const auto type = header.u16le("type", SourceValueDisplay::Hex);
  static_cast<void>(header.u8("reserved", SourceValueDisplay::Hex));
  const auto flags = header.u8("flags", SourceValueDisplay::Hex);
  const auto frequency = header.u32le("frequency");
  const auto encodedLoopStart = header.u32le("loop_start");
  const auto decodedSamples = header.u32le("sample_count");
  if (!header.ok() || *frequency == 0 || *decodedSamples == 0 || *decodedSamples > 0x3fffff) {
    return std::nullopt;
  }

  const bool compressed = *type != 0;
  const u64 encodedBytes = compressed ? ((static_cast<u64>(*decodedSamples) + 63) / 64) * 33 : *decodedSamples;
  if (!builder.reader().has(*offset + 16, encodedBytes)) {
    return std::nullopt;
  }
  u32 loopStart = *encodedLoopStart;
  // The reverse mixer stops at the beginning of the sample; unlike the
  // forward path, it never takes the WaveData loop branch.
  const bool loops = !reverse && (*flags & 0xc0) != 0 && loopStart < *decodedSamples;
  if (loopStart >= *decodedSamples) {
    loopStart = 0;
  }
  unityKey = 60.0 + 12.0 * std::log2(context.sampleRate * 1024.0 / *frequency);
  const auto source = std::move(header).finish();
  const std::string name = fmt::format("Sample {:#x}", *offset);
  auto entry = context.pcmSamples().add(
      sampleKey, Sample{
                     .name = name,
                     .codec = compressed ? AudioCodec::GbaBdpcm : AudioCodec::PcmS8,
                     .encodedData = builder.reader().range(*offset + 16, encodedBytes),
                     .sampleRate = context.sampleRate,
                     .bitsPerSample = 8,
                     .reverse = reverse,
                     .loop = Loop{.enabled = loops, .start = loopStart, .length = *decodedSamples - loopStart},
                     .codecParameter = compressed ? *decodedSamples : 0,
                 });
  entry.source(name + " Header", source, "mp2k-wave-header");
  return entry.ref();
}

[[nodiscard]] std::optional<SampleRef> programmableWave(ScanResultBuilder& builder, ScanSampleCollectionDraft& psg,
                                                        u32 pointer) {
  const auto offset = romOffset(pointer, builder.reader(), 16);
  if (!offset) {
    return std::nullopt;
  }
  const u64 key = kProgrammableWaveKeyBase | *offset;
  if (const auto existing = psg.find(key)) {
    return existing;
  }
  const std::string name = fmt::format("PSG programmable wave {:#x}", *offset);
  auto entry = psg.add(key, Sample{
                                .name = name,
                                .codec = AudioCodec::GbaPsgWave,
                                .encodedData = builder.reader().range(*offset, 16),
                                .sampleRate = 32 * 440,
                                .loop = Loop{.enabled = true, .start = kPsgLoopGuardSamples, .length = 32},
                            });
  entry.source(name, builder.reader().range(*offset, 16), "mp2k-programmable-wave");
  return entry.ref();
}

[[nodiscard]] std::optional<Region> regionForTone(SynthContext& context, const Tone& tone, KeyRange keys,
                                                  std::optional<u8> rhythmKey = std::nullopt) {
  const u8 cgbType = tone.cgbType();
  std::optional<SampleRef> sample;
  double unity = 69.0;
  if (cgbType == 0) {
    sample = addPcmSample(context, tone.wave, tone.reverse(), unity);
  } else if (cgbType == 1 || cgbType == 2) {
    sample = context.psg.find(tone.wave & 3);
  } else if (cgbType == 3) {
    sample = programmableWave(context.builder, context.psg, tone.wave);
  } else if (cgbType == 4) {
    sample = context.psg.find(4 + (tone.wave & 1));
  }
  if (!sample) {
    return std::nullopt;
  }

  const u8 pitchKey = rhythmKey ? tone.key : keys.low;
  if (cgbType == 4) {
    unity = keys.low - 12.0 * std::log2(noiseClockHertz(pitchKey) / context.sampleRate);
  } else if (cgbType >= 1 && cgbType <= 3) {
    unity = keys.low -
            12.0 * std::log2(cgbClockHertz(cgbType, pitchKey, tone.fixed(), context.dacBits) / kPsgSampleFrequency);
  } else if (cgbType == 0 && tone.fixed()) {
    // SoundMainRAM uses a literal 0x800000 phase increment for FIX voices,
    // so every played key must reproduce the sample at the mixer rate.
    unity = keys.low;
  } else if (rhythmKey) {
    unity += static_cast<s32>(*rhythmKey) - tone.key;
  }
  double pan = 0.5;
  if (rhythmKey && (tone.panSweep & 0x80) != 0) {
    const s8 rhythmPan = static_cast<s8>(static_cast<u8>((tone.panSweep + 0x40) * 2));
    const double position = (2.0 * rhythmPan + 1.0) / 255.0;
    pan = (position + 1.0) * 0.5;
  }
  return Region{
      .keyRange = keys,
      .sample = *sample,
      .range = tone.range,
      .unityKey = unity,
      .envelope = envelopeFor(tone, cgbType != 0),
      .pan = pan,
      // The driver collapses the CGB voice's two channel-volume bytes into
      // one envelope lane; DirectSound retains and mixes both lanes.
      .attenuationDb =
          cgbType == 0 ? directSoundMasterAttenuation(context.directSoundMasterVolume) : kPsgDacAttenuationDb,
  };
}

void addToneRegion(SynthContext& context, InstrumentSetBuilder::Entry instrument, const Tone& tone, KeyRange keys,
                   std::optional<u8> rhythmKey = std::nullopt) {
  const u8 cgbType = tone.cgbType();
  const bool separateKeys = keys.low != keys.high && ((cgbType >= 1 && cgbType <= 4) || (cgbType == 0 && tone.fixed()));
  if (separateKeys) {
    for (u32 key = keys.low; key <= keys.high; ++key) {
      const KeyRange singleKey{.low = static_cast<u8>(key), .high = static_cast<u8>(key)};
      if (auto region = regionForTone(context, tone, singleKey)) {
        instrument.region(region->sample, *region).source("Tone", tone.source, "mp2k-tone");
      }
    }
    return;
  }
  if (auto region = regionForTone(context, tone, keys, rhythmKey)) {
    instrument.region(region->sample, *region).source("Tone", tone.source, "mp2k-tone");
  }
}

void addSplitRegions(SynthContext& context, InstrumentSetBuilder::Entry instrument, const Tone& tone) {
  const auto tones = romOffset(tone.wave, context.builder.reader(), 12);
  const auto keymap = romOffset(context.builder.reader().le32(tone.range.offset + 8), context.builder.reader(), 128);
  if (!tones || !keymap) {
    return;
  }

  for (u32 low = 0; low < 128;) {
    const u8 index = context.builder.reader().u8At(*keymap + low);
    u32 high = low;
    while (high + 1 < 128 && context.builder.reader().u8At(*keymap + high + 1) == index) {
      ++high;
    }
    if (const auto sub = readTone(context.builder, *tones + static_cast<u32>(index) * 12); sub && !sub->table()) {
      addToneRegion(context, instrument, *sub, KeyRange{.low = static_cast<u8>(low), .high = static_cast<u8>(high)});
    }
    low = high + 1;
  }
}

void addRhythmRegions(SynthContext& context, InstrumentSetBuilder::Entry instrument, const Tone& tone) {
  const auto tones = romOffset(tone.wave, context.builder.reader(), 128 * 12);
  if (!tones) {
    return;
  }
  for (u32 key = 0; key < 128; ++key) {
    if (const auto drum = readTone(context.builder, *tones + key * 12); drum && !drum->table()) {
      addToneRegion(context, instrument, *drum, KeyRange{.low = static_cast<u8>(key), .high = static_cast<u8>(key)},
                    static_cast<u8>(key));
    }
  }
}

}  // namespace

ScanSampleCollectionDraft addMp2kPsgSamples(ScanResultBuilder& builder, u32 sampleRate) {
  auto samples = builder.sampleCollection("MP2k PSG samples");
  constexpr std::array<std::string_view, 4> names{"12.5%", "25%", "50%", "75%"};
  for (u32 duty = 0; duty < names.size(); ++duty) {
    samples.add(duty, Sample{
                          .name = fmt::format("PSG square {}", names[duty]),
                          .codec = AudioCodec::GbaPsg,
                          .encodedData = builder.reader().range(0, 0),
                          .sampleRate = sampleRate,
                          .loop = Loop{.enabled = true, .start = kPsgLoopGuardSamples, .length = sampleRate},
                          .codecParameter = duty,
                      });
  }
  constexpr std::array noise{
      std::pair{"PSG noise (15-bit)", 32767u},
      std::pair{"PSG noise (7-bit)", 127u},
  };
  for (u32 index = 0; index < noise.size(); ++index) {
    const u32 key = 4 + index;
    samples.add(key, Sample{
                         .name = noise[index].first,
                         .codec = AudioCodec::GbaPsg,
                         .encodedData = builder.reader().range(0, 0),
                         .sampleRate = sampleRate,
                         .loop = Loop{.enabled = true, .start = kPsgLoopGuardSamples, .length = noise[index].second},
                         .codecParameter = key,
                     });
  }
  return samples;
}

ScanInstrumentSetDraft addMp2kInstrumentSet(ScanResultBuilder& builder, const Mp2kBank& bank, u32 sampleRate,
                                            u8 directSoundMasterVolume, u8 dacBits, ScanSampleCollectionDraft& psg,
                                            std::optional<ScanSampleCollectionDraft>& pcmAsset) {
  auto instruments = builder.instrumentSet(fmt::format("MP2k bank {:#x}", bank.offset));
  SynthContext context{
      .builder = builder,
      .sampleRate = sampleRate,
      .directSoundMasterVolume = directSoundMasterVolume,
      .dacBits = dacBits,
      .psg = psg,
      .pcm = pcmAsset,
      .bankOffset = bank.offset,
  };
  instruments.include(builder.reader().range(bank.offset, static_cast<u64>(bank.instrumentCount) * 12));
  instruments.source(SourceRole::Table, "Voicegroup", instruments.range(), "mp2k-voicegroup")
      .derived("instrument_count", bank.instrumentCount);

  for (u32 program = 0; program < bank.instrumentCount; ++program) {
    const auto tone = readTone(builder, bank.offset + program * 12);
    if (!tone || (tone->type == 1 && tone->wave == 2 && builder.reader().le32(tone->range.offset + 8) == 0x000f0000)) {
      continue;
    }
    const u8 cgbType = tone->cgbType();
    const bool tableTone = tone->table();
    const bool playable = tableTone || cgbType == 1 || cgbType == 2 || cgbType == 4 ||
                          (cgbType == 0 && romOffset(tone->wave, builder.reader(), 16)) ||
                          (cgbType == 3 && romOffset(tone->wave, builder.reader(), 16));
    if (!playable) {
      continue;
    }
    Instrument value{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = program},
        .reverb = 0.0,
        .name = fmt::format("Program {}", program),
        .range = tone->range,
        .modulation = mp2kModulation(),
    };
    auto instrument = instruments.builder().append(std::move(value));
    instrument.source("Tone", tone->source, "mp2k-tone");

    if (tone->split() && !tone->rhythm()) {
      addSplitRegions(context, instrument, *tone);
    } else if (tone->rhythm()) {
      addRhythmRegions(context, instrument, *tone);
    } else {
      addToneRegion(context, instrument, *tone, {});
    }
  }
  return instruments;
}

}  // namespace vgmtrans::formats::mp2k
