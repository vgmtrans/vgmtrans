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

std::optional<Mp2kTone> parseMp2kTone(ByteReader reader, u32 offset, std::vector<Diagnostic>* diagnostics) {
  if (!reader.has(offset, 12)) {
    return std::nullopt;
  }
  RecordReader record(reader, offset, offset + 12, diagnostics, diagnostics != nullptr);
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
  return Mp2kTone{
      .type = *type,
      .key = *key,
      .length = *length,
      .panSweep = *panSweep,
      .wave = *wave,
      .attack = *attack,
      .decay = *decay,
      .sustain = *sustain,
      .release = *release,
      .source = std::move(record).finish(),
  };
}

std::optional<Mp2kTone> mp2kToneForKey(ByteReader reader, const Mp2kTone& tone, u8 key,
                                       std::vector<Diagnostic>* diagnostics) {
  if (!tone.table()) {
    return tone;
  }
  u32 index = key;
  if (tone.split()) {
    if (!reader.has(tone.source.range.offset + 8, 4)) {
      return std::nullopt;
    }
    const auto keymap = romOffset(reader.le32(tone.source.range.offset + 8), reader, 128);
    if (!keymap) {
      return std::nullopt;
    }
    index = reader.u8At(*keymap + key);
  }
  const auto tones = romOffset(tone.wave, reader);
  return tones ? parseMp2kTone(reader, *tones + index * 12, diagnostics) : std::nullopt;
}

Envelope mp2kEnvelope(const Mp2kTone& tone) {
  const bool cgb = tone.cgbType() != 0;
  return Envelope{
      .attackSeconds = cgb ? cgbEnvelopeSeconds(tone.attack) : directAttackSeconds(tone.attack),
      // Decay begins after one CGB envelope period or the next DirectSound mixer pass.
      .holdSeconds = cgb ? cgbEnvelopeSeconds(tone.decay, 1) : 1.0 / kGbaMixerFrameRate,
      .decaySeconds = cgb ? cgbDecaySeconds(tone.decay) : directDecaySeconds(tone.decay),
      .releaseSeconds = cgb ? cgbDecaySeconds(tone.release) : directDecaySeconds(tone.release),
      .sustainAmplitude = cgb ? std::min<u8>(tone.sustain, 15) / 15.0 : tone.sustain / 255.0,
  };
}

namespace {

constexpr double kPsgSampleFrequency = 440.0;
constexpr u32 kPsgRenderSampleRate = 44100;
constexpr u32 kPsgLoopGuardSamples = 8;
constexpr u32 kGbaCpuFrequency = 16777216;
constexpr std::array<u8, 60> kNoiseRegisters{
    0xd7, 0xd6, 0xd5, 0xd4, 0xc7, 0xc6, 0xc5, 0xc4, 0xb7, 0xb6, 0xb5, 0xb4, 0xa7, 0xa6, 0xa5,
    0xa4, 0x97, 0x96, 0x95, 0x94, 0x87, 0x86, 0x85, 0x84, 0x77, 0x76, 0x75, 0x74, 0x67, 0x66,
    0x65, 0x64, 0x57, 0x56, 0x55, 0x54, 0x47, 0x46, 0x45, 0x44, 0x37, 0x36, 0x35, 0x34, 0x27,
    0x26, 0x25, 0x24, 0x17, 0x16, 0x15, 0x14, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
};
constexpr std::array<s16, 12> kCgbFrequencyTable{
    -2004, -1891, -1785, -1685, -1591, -1501, -1417, -1337, -1262, -1192, -1125, -1062,
};
constexpr std::array<u32, 12> kDirectSoundFrequencyTable{
    2147483648u, 2275179671u, 2410468894u, 2553802834u, 2705659852u, 2866546760u,
    3037000500u, 3217589947u, 3408917802u, 3611622603u, 3826380858u, 4053909305u,
};
constexpr std::array<std::string_view, 4> kSquareNames{"PSG square 12.5%", "PSG square 25%", "PSG square 50%",
                                                       "PSG square 75%"};
constexpr std::array<std::string_view, 2> kNoiseNames{"PSG noise (15-bit)", "PSG noise (7-bit)"};
constexpr std::array<u32, 2> kNoisePeriods{32767, 127};

struct SynthContext {
  ScanResultBuilder& builder;
  u32 sampleRate;
  u8 directSoundMasterVolume;
  u8 dacBits;
  SamplePoolBuilder& psg;
  SamplePoolBuilder& pcm;
};

[[nodiscard]] double directSoundMasterAttenuation(u8 volume) {
  // The software mixer scales its 8-bit envelope by (masterVolume + 1) / 16
  // before applying the channel's linear left/right volume bytes.
  return -20.0 * std::log10((volume + 1.0) / 16.0);
}

[[nodiscard]] u32 directSoundPhaseStep(u32 waveFrequency, u8 key, u32 sampleRate) {
  const u32 scale = kDirectSoundFrequencyTable[key % 12] >> (14 - key / 12);
  const u32 frequency = static_cast<u32>((static_cast<u64>(waveFrequency) * scale) >> 32);
  const u32 divisor = (kGbaCpuFrequency / sampleRate + 1) >> 1;
  return std::max<u32>(1, divisor * frequency);
}

[[nodiscard]] double noiseClockHertz(u8 key) {
  const u8 index = key <= 20 ? 0 : std::min<u8>(key - 21, 59);
  const u8 reg = kNoiseRegisters[index];
  const double divisor = (reg & 7) == 0 ? 0.5 : reg & 7;
  return 524288.0 / divisor / std::exp2((reg >> 4) + 1.0);
}

[[nodiscard]] u16 cgbFrequencyRegister(u8 key, bool fixed, u8 dacBits) {
  const u8 index = key <= 35 ? 0 : std::min<u8>(key - 36, 130);
  const u32 octave = index / 12;
  u16 frequency = static_cast<u16>(2048 + (kCgbFrequencyTable[index % 12] >> octave));
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

[[nodiscard]] u32 psgReferencePeriod(double hertz, u32 sampleRate) {
  // Reuse one representative period per octave so a bank does not need a
  // separately rendered sample for every key.
  const s32 octave = std::clamp<s32>(std::lround(std::log2(hertz / kPsgSampleFrequency)), -3, 4);
  const double referenceHertz = std::ldexp(kPsgSampleFrequency, octave);
  return std::max<u32>(1, std::lround(sampleRate / referenceHertz));
}

[[nodiscard]] InstrumentModulation mp2kModulation() {
  const ModulationRange rate{.minimum = 0.0, .maximum = 127.0 * kGbaMixerFrameRate / 256.0};
  const ModulationRange delay{.minimum = 0.0, .maximum = 255.0 / kGbaMixerFrameRate};
  return InstrumentModulation{
      .vibrato =
          VibratoSpec{
              .maxDepthCents = 127.0 * 100.0 / 16.0,
              .rateHertz = rate,
              .waveform = LfoWaveform::Triangle,
              .delaySeconds = delay,
          },
      .tremolo =
          TremoloSpec{
              .maxDepthDb = 20.0 * std::log10(1.0 + 127.0 / 128.0),
              .rateHertz = rate,
              .waveform = LfoWaveform::Triangle,
              .gainMode = TremoloGainMode::BipolarAroundNominal,
              .delaySeconds = delay,
          },
  };
}

[[nodiscard]] std::optional<SampleRef> addPcmSample(SynthContext& context, const Mp2kTone& tone,
                                                    std::optional<u8> rhythmKey, double& unityKey) {
  auto& builder = context.builder;
  const auto offset = romOffset(tone.wave, builder.reader(), 16);
  if (!offset) {
    return std::nullopt;
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
  const double naturalKey = 60.0 + 12.0 * std::log2(context.sampleRate * 1024.0 / *frequency);
  const u8 sourceKey = rhythmKey ? tone.key : static_cast<u8>(std::clamp(std::lround(naturalKey), 0l, 127l));
  const u32 phaseStep = tone.fixed() ? 0 : directSoundPhaseStep(*frequency, sourceKey, context.sampleRate);
  const u64 sampleKey = (static_cast<u64>(tone.reverse()) << 57) | (static_cast<u64>(phaseStep) << 25) | *offset;
  unityKey = sourceKey;
  if (const auto existing = context.pcm.find(sampleKey)) {
    return existing;
  }
  u32 loopStart = *encodedLoopStart;
  // The reverse mixer stops at the beginning of the sample; unlike the
  // forward path, it never takes the WaveData loop branch.
  const bool loops = !tone.reverse() && (*flags & 0xc0) != 0 && loopStart < *decodedSamples;
  if (loopStart >= *decodedSamples) {
    loopStart = 0;
  }
  const auto source = std::move(header).finish();
  const std::string name = fmt::format("Sample {:#x}", *offset);
  auto entry = context.pcm.add(
      sampleKey, Sample{
                     .name = name,
                     .codec = AudioCodec::GbaDirectSound,
                     .encodedData = builder.reader().range(*offset, encodedBytes + 16),
                     .sampleRate = kGbaCpuFrequency >> context.dacBits,
                     .bitsPerSample = 8,
                     .reverse = tone.reverse(),
                     .loop = Loop{.enabled = loops, .start = loopStart, .length = *decodedSamples - loopStart},
                     // High word: software-mixer rate; low word: Q23 source-phase increment.
                     .codecParameter = (static_cast<u64>(context.sampleRate) << 32) | phaseStep,
                 });
  entry.source(name + " Header", source, "mp2k-wave-header");
  return entry.ref();
}

[[nodiscard]] std::optional<SampleRef> programmableWave(SynthContext& context, u32 pointer, u32 sampleRate,
                                                        u32 period) {
  const auto offset = romOffset(pointer, context.builder.reader(), 16);
  if (!offset) {
    return std::nullopt;
  }
  const u64 key = (static_cast<u64>(period) << 32) | *offset;
  if (const auto existing = context.psg.find(key)) {
    return existing;
  }
  const std::string name = fmt::format("PSG programmable wave {:#x}", *offset);
  auto entry = context.psg.add(key, Sample{
                                        .name = name,
                                        .codec = AudioCodec::GbaPsgWave,
                                        .encodedData = context.builder.reader().range(*offset, 16),
                                        .sampleRate = sampleRate,
                                        .loop = Loop{.enabled = true, .start = kPsgLoopGuardSamples, .length = period},
                                    });
  entry.source(name, context.builder.reader().range(*offset, 16), "mp2k-programmable-wave");
  return entry.ref();
}

[[nodiscard]] SampleRef generatedPsgSample(SynthContext& context, u64 key, std::string_view name, u32 sampleRate,
                                           u32 period, u32 parameter) {
  if (const auto existing = context.psg.find(key)) {
    return *existing;
  }
  return context.psg
      .add(key,
           Sample{
               .name = std::string(name),
               .codec = AudioCodec::GbaPsg,
               .encodedData = context.builder.reader().range(0, 0),
               .sampleRate = sampleRate,
               .loop = Loop{.enabled = true, .start = kPsgLoopGuardSamples, .length = period},
               .codecParameter = parameter,
           })
      .ref();
}

[[nodiscard]] std::optional<Region> regionForTone(SynthContext& context, const Mp2kTone& tone, KeyRange keys,
                                                  std::optional<u8> rhythmKey = std::nullopt) {
  const u8 cgbType = tone.cgbType();
  const bool pitchedPsg = cgbType >= 1 && cgbType <= 3;
  const u8 pitchKey = rhythmKey ? tone.key : keys.low;
  const double cgbHertz = pitchedPsg ? cgbClockHertz(cgbType, pitchKey, tone.fixed(), context.dacBits) : 0.0;
  const u32 psgSampleRate = std::min(kPsgRenderSampleRate, kGbaCpuFrequency >> context.dacBits);
  const u32 psgPeriod = pitchedPsg ? psgReferencePeriod(cgbHertz, psgSampleRate) : 0;
  std::optional<SampleRef> sample;
  double unity = 69.0;
  if (cgbType == 0) {
    sample = addPcmSample(context, tone, rhythmKey, unity);
  } else if (cgbType == 1 || cgbType == 2) {
    const u32 duty = tone.wave & 3;
    sample = generatedPsgSample(context, psgPeriod * 4 + duty, kSquareNames[duty], psgSampleRate, psgPeriod, duty);
  } else if (cgbType == 3) {
    sample = programmableWave(context, tone.wave, psgSampleRate, psgPeriod);
  } else if (cgbType == 4) {
    const u32 width = tone.wave & 1;
    sample =
        generatedPsgSample(context, 4 + width, kNoiseNames[width], context.sampleRate, kNoisePeriods[width], 4 + width);
  }
  if (!sample) {
    return std::nullopt;
  }

  if (cgbType == 4) {
    unity = keys.low - 12.0 * std::log2(noiseClockHertz(pitchKey) / context.sampleRate);
  } else if (pitchedPsg) {
    unity = keys.low - 12.0 * std::log2(cgbHertz / (static_cast<double>(psgSampleRate) / psgPeriod));
  } else if (cgbType == 0 && tone.fixed()) {
    // SoundMainRAM uses a literal 0x800000 phase increment for FIX voices,
    // so every played key must reproduce the sample at the mixer rate.
    unity = keys.low;
  } else if (rhythmKey) {
    unity += static_cast<s32>(*rhythmKey) - tone.key;
  }
  double pan = 0.5;
  if (cgbType == 0 && rhythmKey && (tone.panSweep & 0x80) != 0) {
    const s8 rhythmPan = static_cast<s8>(static_cast<u8>((tone.panSweep + 0x40) * 2));
    const double position = (2.0 * rhythmPan + 1.0) / 255.0;
    pan = (position + 1.0) * 0.5;
  }
  return Region{
      .keyRange = keys,
      .sample = *sample,
      .range = tone.source.range,
      .unityKey = unity,
      .envelope = mp2kEnvelope(tone),
      .pan = pan,
      .attenuationDb = cgbType == 0 ? directSoundMasterAttenuation(context.directSoundMasterVolume) : 0.0,
  };
}

void addToneRegion(SynthContext& context, InstrumentSetBuilder::Entry instrument, const Mp2kTone& tone, KeyRange keys,
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

void addSplitRegions(SynthContext& context, InstrumentSetBuilder::Entry instrument, const Mp2kTone& tone) {
  const auto keymap =
      romOffset(context.builder.reader().le32(tone.source.range.offset + 8), context.builder.reader(), 128);
  if (!keymap) {
    return;
  }

  for (u32 low = 0; low < 128;) {
    const u8 index = context.builder.reader().u8At(*keymap + low);
    u32 high = low;
    while (high + 1 < 128 && context.builder.reader().u8At(*keymap + high + 1) == index) {
      ++high;
    }
    if (const auto sub =
            mp2kToneForKey(context.builder.reader(), tone, static_cast<u8>(low), &context.builder.diagnostics());
        sub && !sub->table()) {
      addToneRegion(context, instrument, *sub, KeyRange{.low = static_cast<u8>(low), .high = static_cast<u8>(high)});
    }
    low = high + 1;
  }
}

void addRhythmRegions(SynthContext& context, InstrumentSetBuilder::Entry instrument, const Mp2kTone& tone) {
  for (u32 key = 0; key < 128; ++key) {
    if (const auto drum =
            mp2kToneForKey(context.builder.reader(), tone, static_cast<u8>(key), &context.builder.diagnostics());
        drum && !drum->table()) {
      addToneRegion(context, instrument, *drum, KeyRange{.low = static_cast<u8>(key), .high = static_cast<u8>(key)},
                    static_cast<u8>(key));
    }
  }
}

std::vector<Mp2kTone> parseMp2kTones(ByteReader reader, const Mp2kBank& bank, std::vector<Diagnostic>* diagnostics) {
  std::vector<Mp2kTone> result;
  result.reserve(bank.instrumentCount);
  for (u32 program = 0; program < bank.instrumentCount; ++program) {
    auto tone = parseMp2kTone(reader, bank.offset + program * 12, diagnostics);
    if (!tone) {
      break;
    }
    result.push_back(std::move(*tone));
  }
  return result;
}

}  // namespace

Mp2kScannedBank addMp2kInstrumentSet(ScanResultBuilder& builder, const Mp2kBank& bank, const Mp2kEngine& engine,
                                     ScanSamplePoolDraft& psg) {
  std::vector<Mp2kTone> tones = parseMp2kTones(builder.reader(), bank, &builder.diagnostics());
  auto bankDraft = builder.soundBank(fmt::format("MP2k bank {:#x}", bank.offset));
  auto& instruments = bankDraft.instruments();
  SynthContext context{
      .builder = builder,
      .sampleRate = engine.sampleRate,
      .directSoundMasterVolume = engine.directSoundMasterVolume,
      .dacBits = engine.dacBits,
      .psg = psg.samples(),
      .pcm = bankDraft.localSamples(),
  };
  instruments.include(builder.reader().range(bank.offset, static_cast<u64>(bank.instrumentCount) * 12));
  instruments.source(SourceRole::Table, "Voicegroup", instruments.range(), "mp2k-voicegroup")
      .derived("instrument_count", bank.instrumentCount);

  for (u32 program = 0; program < tones.size(); ++program) {
    const Mp2kTone& tone = tones[program];
    if (tone.type == 1 && tone.wave == 2 && builder.reader().le32(tone.source.range.offset + 8) == 0x000f0000) {
      continue;
    }
    const u8 cgbType = tone.cgbType();
    const bool tableTone = tone.table();
    const bool playable = tableTone || cgbType == 1 || cgbType == 2 || cgbType == 4 ||
                          (cgbType == 0 && romOffset(tone.wave, builder.reader(), 16)) ||
                          (cgbType == 3 && romOffset(tone.wave, builder.reader(), 16));
    if (!playable) {
      continue;
    }
    Instrument value{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = program},
        .reverb = 0.0,
        .name = fmt::format("Program {}", program),
        .range = tone.source.range,
        .modulation = mp2kModulation(),
    };
    auto instrument = instruments.append(std::move(value));
    instrument.source("Tone", tone.source, "mp2k-tone");

    if (tone.split() && !tone.rhythm()) {
      addSplitRegions(context, instrument, tone);
    } else if (tone.rhythm()) {
      addRhythmRegions(context, instrument, tone);
    } else {
      addToneRegion(context, instrument, tone, {});
    }
  }
  return Mp2kScannedBank{.instruments = bankDraft, .tones = std::move(tones)};
}

}  // namespace vgmtrans::formats::mp2k
