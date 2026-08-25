/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SegSat/SegSat.h"

#include "value/base/RecordReader.h"
#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <utility>

namespace vgmtrans::formats::segsat {

using namespace core;

namespace {

constexpr std::array<double, 64> kAttackMilliseconds{
    100000, 100000, 8100, 6900, 6000, 4800, 4000, 3400, 3000, 2400, 2000, 1700, 1500, 1200, 1000, 860,
    760,    600,    500,  430,  380,  300,  250,  220,  190,  150,  130,  110,  95,   76,   63,   55,
    47,     38,     31,   27,   24,   19,   15,   13,   12,   9.4,  7.9,  6.8,  6.0,  4.7,  3.8,  3.4,
    3.0,    2.4,    2.0,  1.8,  1.6,  1.3,  1.1,  0.93, 0.85, 0.65, 0.53, 0.44, 0.40, 0.35, 0.0,  0.0};
constexpr std::array<double, 64> kDecayMilliseconds{
    100000, 100000, 118200, 101300, 88600, 70900, 59100, 50700, 44300, 35500, 29600, 25300, 22200, 17700, 14800, 12700,
    11100,  8900,   7400,   6300,   5500,  4400,  3700,  3200,  2800,  2200,  1800,  1600,  1400,  1100,  920,   790,
    690,    550,    460,    390,    340,   270,   230,   200,   170,   140,   110,   98,    85,    68,    57,    49,
    43,     34,     28,     25,     22,    18,    14,    12,    11,    8.5,   7.1,   6.1,   5.4,   4.3,   3.6,   3.1};
constexpr std::array<double, 8> kDirectLevelAttenuation{1000000.0, 36.0, 30.0, 24.0, 18.0, 12.0, 6.0, 0.0};

struct SampleData {
  u32 offset = 0;
  u32 bytes = 0;
  u32 loopStartFrames = 0;
  u32 loopLengthFrames = 0;
  bool loops = false;
  bool reverse = false;
  bool pcm16 = false;
};

struct ParsedRegion {
  SourceRecord source;
  SampleData sample;
  Region region;
  u8 vlIndex = 0;
  u8 totalLevel = 0;
  InstrumentModulation modulation;
};

struct ParsedInstrument {
  u32 index = 0;
  SourceRange range;
  s8 volumeBias = 0;
  u16 pitchBendRangeCents = 200;
  std::vector<ParsedRegion> regions;
};

[[nodiscard]] bool rangeValid(ByteReader reader, u64 offset, u64 size) {
  return offset <= reader.size() && size <= reader.size() - offset;
}

[[nodiscard]] SourceRecord regionSource(ByteReader reader, u32 offset) {
  RecordReader record(reader, offset, offset + 0x20);
  // Parsing below uses these values directly. RecordReader is used here to
  // retain the exact byte range for every TreeView child.
  (void)record.u8At(0, "key_low", SourceValueDisplay::MidiNote);
  (void)record.u8At(1, "key_high", SourceValueDisplay::MidiNote);
  (void)record.u32beAt(2, "address_and_flags", SourceValueDisplay::Hex);
  (void)record.u16beAt(6, "loop_start");
  (void)record.u16beAt(8, "loop_end");
  (void)record.u16beAt(10, "adsr_1", SourceValueDisplay::Hex);
  (void)record.u16beAt(12, "adsr_2", SourceValueDisplay::Hex);
  (void)record.u8At(14, "modulation_flags", SourceValueDisplay::Hex);
  (void)record.u8At(15, "total_level");
  (void)record.u16beAt(16, "pitch", SourceValueDisplay::Hex);
  (void)record.u16beAt(18, "modulation", SourceValueDisplay::Hex);
  (void)record.u16beAt(20, "lfo", SourceValueDisplay::Hex);
  (void)record.u16beAt(22, "effect_output", SourceValueDisplay::Hex);
  (void)record.u8At(24, "direct_output", SourceValueDisplay::Hex);
  (void)record.u8At(25, "unity_key", SourceValueDisplay::MidiNote);
  (void)record.s8At(26, "fine_tune");
  (void)record.u8At(27, "reserved_27", SourceValueDisplay::Hex);
  (void)record.u8At(28, "reserved_28", SourceValueDisplay::Hex);
  (void)record.u8At(29, "velocity_table");
  (void)record.u8At(30, "peg_table");
  (void)record.u8At(31, "plfo_table");
  return std::move(record).finish();
}

[[nodiscard]] u16 pitchBendRangeCents(u8 voiceHeader) {
  const u8 encoded = voiceHeader & 0x0f;
  // The Tone Editor defines 0-12 as semitones and 13 as two octaves.
  // It does not produce 14 or 15, so use those values literally if a bank
  // contains either one.
  const u8 semitones = encoded == 13 ? 24 : encoded;
  return static_cast<u16>(semitones) * 100;
}

[[nodiscard]] std::optional<SampleData> readSampleData(ByteReader reader, const SegSatBankLayout& bank,
                                                       u32 regionOffset) {
  const u32 addressAndFlags = reader.be32(regionOffset + 2);
  const bool pcm16 = ((addressAndFlags >> 20) & 1) == 0;
  const u32 bytesPerFrame = pcm16 ? 2 : 1;
  u32 sampleOffset = addressAndFlags & 0x7ffff;
  if (pcm16) {
    sampleOffset &= ~1u;
  }
  sampleOffset += bank.offset;
  const u32 loopStartFrames = reader.be16(regionOffset + 6);
  const u32 loopEndFrames = reader.be16(regionOffset + 8);
  if (sampleOffset <= bank.offset || sampleOffset >= bank.offset + 0x7fffe || loopEndFrames < loopStartFrames ||
      !rangeValid(reader, sampleOffset, static_cast<u64>(loopEndFrames) * bytesPerFrame)) {
    return std::nullopt;
  }
  const u8 loopMode = static_cast<u8>((reader.u8At(regionOffset + 3) >> 5) & 3);
  return SampleData{
      .offset = sampleOffset,
      .bytes = loopEndFrames * bytesPerFrame,
      .loopStartFrames = loopStartFrames,
      .loopLengthFrames = loopEndFrames - loopStartFrames,
      .loops = loopMode != 0,
      .reverse = loopMode == 2,
      .pcm16 = pcm16,
  };
}

[[nodiscard]] double scspRateSeconds(const std::array<double, 64>& milliseconds, u8 rate, u8 keyRateScaling) {
  // The rate also changes with the played note. At the region's unity pitch,
  // OCT and FNS add nothing, leaving only the KRS contribution.
  const u8 rateBase = keyRateScaling == 15 ? 0 : static_cast<u8>(keyRateScaling * 2);
  const u8 index = std::min<u8>(static_cast<u8>(rate * 2 + rateBase), 63);
  if (index < 2) {
    return std::numeric_limits<double>::infinity();
  }
  return milliseconds[index] / 1000.0;
}

[[nodiscard]] Envelope scspEnvelope(u16 adsr1, u16 adsr2) {
  const u8 attack = static_cast<u8>(adsr1 & 0x1f);
  const bool holdAttack = (adsr1 & 0x20) != 0;
  const u8 decay1 = static_cast<u8>((adsr1 >> 6) & 0x1f);
  const u8 decay2 = static_cast<u8>((adsr1 >> 11) & 0x1f);
  const u8 release = static_cast<u8>(adsr2 & 0x1f);
  const u8 decayLevel = static_cast<u8>((adsr2 >> 5) & 0x1f);
  const u8 keyRateScaling = static_cast<u8>((adsr2 >> 10) & 0x0f);

  double attackSeconds = scspRateSeconds(kAttackMilliseconds, attack, keyRateScaling);
  if (std::isfinite(attackSeconds)) {
    // The rate table covers all 1023 envelope steps, but SCSP attack starts at
    // step 0x17f (-60 dB), leaving 640 steps before full volume.
    attackSeconds *= 640.0 / 1023.0;
  }

  double decaySeconds = scspRateSeconds(kDecayMilliseconds, decay1, keyRateScaling);
  if (decayLevel == 0) {
    decaySeconds = 0.0;
  }
  // D1R is a full-scale rate. DL determines how much of it elapses before the
  // chip switches to D2R; scaling it here would make SF2/DLS scale it twice.

  // Unlike the other rates, D2R 0 always holds its current level even when
  // key-rate scaling would otherwise produce a nonzero effective rate.
  const double secondDecaySeconds = decay2 == 0 ? std::numeric_limits<double>::infinity()
                                                : scspRateSeconds(kDecayMilliseconds, decay2, keyRateScaling);

  return Envelope{
      .attackSeconds = holdAttack ? 0.0 : attackSeconds,
      .holdSeconds = holdAttack ? std::optional{attackSeconds} : std::nullopt,
      .decaySeconds = decaySeconds,
      .secondDecaySeconds = secondDecaySeconds,
      .releaseSeconds = scspRateSeconds(kDecayMilliseconds, release, keyRateScaling),
      .sustainAmplitude = std::pow(10.0, (-3.0 * decayLevel) / 20.0),
  };
}

struct PanAndAttenuation {
  double position = 0.5;
  double attenuationDb = 0.0;
};

[[nodiscard]] PanAndAttenuation directOutput(u8 encoded) {
  const u8 directLevel = encoded >> 5;
  const u8 directPan = encoded & 0x1f;
  double panAttenuation = 0.0;
  if (directPan & 1) {
    panAttenuation += 3.0;
  }
  if (directPan & 2) {
    panAttenuation += 6.0;
  }
  if (directPan & 4) {
    panAttenuation += 12.0;
  }
  if (directPan & 8) {
    panAttenuation += 24.0;
  }
  const double panGain = (directPan & 0x0f) == 0x0f ? 0.0 : std::pow(10.0, -panAttenuation / 20.0);
  const double left = directPan < 0x10 ? panGain : 1.0;
  const double right = directPan < 0x10 ? 1.0 : panGain;

  double position = 0.5;
  u8 midiPan = 64;
  if (right == 0.0) {
    position = 0.0;
    midiPan = 0;
  } else if (left == right) {
    position = 0.5;
  } else if (left == 0.0) {
    position = 1.0;
    midiPan = 127;
  } else {
    position = right / (left + right);
    const double arc = std::atan2(position, 1.0 - position) / (std::numbers::pi / 2.0);
    midiPan = static_cast<u8>(std::lround(arc * 126.0));
    if (midiPan != 0) {
      ++midiPan;
    }
  }

  double midiLeft = 0.0;
  double midiRight = 0.0;
  if (midiPan <= 1) {
    midiLeft = 1.0;
  } else if (midiPan == 64) {
    midiLeft = midiRight = std::sqrt(2.0) / 2.0;
  } else if (midiPan == 127) {
    midiRight = 1.0;
  } else {
    const double arc = (midiPan - 1) / 126.0 * (std::numbers::pi / 2.0);
    midiLeft = std::cos(arc);
    midiRight = std::sin(arc);
  }
  const double volumeScale = (left + right) / (midiLeft + midiRight);
  const double balanceAttenuation = volumeScale <= 0.0 ? 100.0 : std::min(100.0, -20.0 * std::log10(volumeScale));
  return PanAndAttenuation{
      .position = position,
      .attenuationDb = kDirectLevelAttenuation[directLevel] + balanceAttenuation + 3.0103,
  };
}

[[nodiscard]] double driverRate(SegSatDriverVersion version) {
  const double irq = 44100.0 / (2.0 * (255.0 - 0xd4));
  return version == SegSatDriverVersion::V2_20 ? irq : irq / 4.0;
}

[[nodiscard]] LfoWaveform scspLfoWaveform(u8 encoded) {
  switch (encoded & 3) {
    case 0:
      return LfoWaveform::SawtoothUp;
    case 1:
      return LfoWaveform::Square;
    case 2:
      return LfoWaveform::Triangle;
    case 3:
      return LfoWaveform::Noise;
  }
  return LfoWaveform::Noise;
}

[[nodiscard]] std::optional<VibratoSpec> softwarePlfoVibrato(ByteReader reader, const SegSatBankLayout& bank,
                                                             SegSatDriverVersion version, u32 offset) {
  const u8 index = reader.u8At(offset + 31);
  const u32 table = bank.offset + bank.plfoTables + static_cast<u32>(index) * 4;
  if (table + 4 > bank.offset + bank.firstInstrument || !rangeValid(reader, table, 4)) {
    return std::nullopt;
  }

  const u8 delay = reader.u8At(table);
  const u8 amplitude = reader.u8At(table + 1);
  const u8 frequency = reader.u8At(table + 2);
  const u8 fade = reader.u8At(table + 3);
  if (frequency == 0) {
    return std::nullopt;
  }

  const double updates = driverRate(version);
  const u32 fadeStep = (static_cast<u32>(fade) * fade) >> 6;
  const double fadeSeconds =
      fadeStep == 0 ? 0.0 : ((fadeStep * 2 - 1) * (static_cast<double>(frequency) * frequency / (64.0 * updates)));
  double depth = (static_cast<double>(amplitude) * amplitude * frequency * frequency) / ((8192.0 * 256.0) / 100.0);
  if (fadeStep != 0) {
    depth *= 2.0 * fadeStep;
  }
  const double rate = (updates * 32.0) / (static_cast<double>(frequency) * frequency);
  const double effectiveDelay = (static_cast<double>(delay) * delay) / (16.0 * updates) + fadeSeconds / 2.0;
  const std::optional<ModulationRange> delayRange =
      effectiveDelay > 0.0
          ? std::optional<ModulationRange>{
                ModulationRange{.minimum = effectiveDelay, .maximum = effectiveDelay},
            }
          : std::nullopt;
  return VibratoSpec{
      .maxDepthCents = depth,
      .rateHertz = {.minimum = rate, .maximum = rate},
      .waveform = LfoWaveform::Triangle,
      // A zero driver delay means "start now." Omitting it avoids lowering
      // that value to the synth formats' finite one-millisecond floor.
      .delaySeconds = delayRange,
      .depthMode = ModulationDepthMode::Fixed,
  };
}

[[nodiscard]] InstrumentModulation regionModulation(ByteReader reader, const SegSatBankLayout& bank,
                                                    SegSatDriverVersion version, u32 offset) {
  InstrumentModulation modulation;
  if ((reader.u8At(offset + 2) & 0x40) != 0) {
    modulation.vibrato = softwarePlfoVibrato(reader, bank, version, offset);
  }

  const u8 lfo0 = reader.u8At(offset + 20);
  const u8 lfo1 = reader.u8At(offset + 21);
  // With this flag set, the driver starts with zero depth and lets modulation
  // wheel events enable the region's stored pitch and amplitude depths.
  const auto depthMode =
      (reader.u8At(offset + 14) & 0x80) != 0 ? ModulationDepthMode::Controller : ModulationDepthMode::Fixed;
  constexpr std::array<double, 32> rates{0.17, 0.19, 0.23, 0.27, 0.34, 0.39, 0.45, 0.55, 0.68, 0.78, 0.92,
                                         1.10, 1.39, 1.60, 1.87, 2.27, 2.87, 3.31, 3.92, 4.79, 6.15, 7.18,
                                         8.60, 10.8, 14.4, 17.2, 21.5, 28.7, 43.1, 57.4, 86.1, 172.3};
  constexpr std::array<double, 8> pitchDepths{0.0, 7.0, 13.5, 27.0, 55.0, 112.0, 230.0, 494.0};
  // SCSP amplitude depths are the full nominal-to-trough attenuation. The
  // shared no-boost LFO is bipolar, so its depth is half of that range.
  constexpr std::array<double, 8> amplitudeDepths{0.0, 0.2, 0.4, 0.75, 1.5, 3.0, 6.0, 12.0};
  const double rate = rates[(lfo0 >> 2) & 0x1f];
  const u8 pitchDepth = lfo1 >> 5;
  // The software PLFO already describes pitch when both mechanisms are enabled.
  if (!modulation.vibrato && pitchDepth != 0) {
    modulation.vibrato = VibratoSpec{
        .maxDepthCents = pitchDepths[pitchDepth],
        .rateHertz = {.minimum = rate, .maximum = rate},
        .waveform = scspLfoWaveform(lfo0),
        .depthMode = depthMode,
    };
  }
  const u8 amplitudeDepth = lfo1 & 7;
  if (amplitudeDepth != 0) {
    modulation.tremolo = TremoloSpec{
        .maxDepthDb = amplitudeDepths[amplitudeDepth],
        .rateHertz = {.minimum = rate, .maximum = rate},
        .waveform = scspLfoWaveform(lfo1 >> 3),
        .gainMode = TremoloGainMode::NoBoost,
        .depthMode = depthMode,
    };
  }
  return modulation;
}

[[nodiscard]] std::vector<ParsedInstrument> parseInstruments(ByteReader reader, const SegSatBankLayout& bank,
                                                             SegSatDriverVersion version) {
  std::vector<ParsedInstrument> instruments;
  for (u32 index = 0; index < bank.instrumentCount; ++index) {
    const u32 offset = bank.offset + reader.be16(bank.offset + 8 + index * 2);
    const u32 regionCount = segSatRegionCount(reader.u8At(offset + 2));
    ParsedInstrument instrument{
        .index = index,
        .range = reader.range(offset, 4 + regionCount * 0x20),
        .volumeBias = static_cast<s8>(reader.u8At(offset + 3)),
        .pitchBendRangeCents = pitchBendRangeCents(reader.u8At(offset)),
    };

    for (u32 regionIndex = 0; regionIndex < regionCount; ++regionIndex) {
      const u32 regionOffset = offset + 4 + regionIndex * 0x20;
      const u8 keyLow = reader.u8At(regionOffset);
      const u8 keyHigh = reader.u8At(regionOffset + 1);
      const auto sample = readSampleData(reader, bank, regionOffset);
      if (keyLow == 0xff || keyLow > keyHigh || !sample) {
        continue;
      }

      const s8 fine = static_cast<s8>(reader.u8At(regionOffset + 26));
      const s16 fineCents = static_cast<s16>((fine / 128.0) * 50.0);
      const auto output = directOutput(reader.u8At(regionOffset + 24));
      ParsedRegion parsed{
          .source = regionSource(reader, regionOffset),
          .sample = *sample,
          .region =
              Region{
                  .keyRange = {.low = keyLow, .high = keyHigh},
                  .range = reader.range(regionOffset, 0x20),
                  .unityKey = reader.u8At(regionOffset + 25) - (fineCents / 100.0),
                  .envelope = scspEnvelope(reader.be16(regionOffset + 10), reader.be16(regionOffset + 12)),
                  .loop =
                      Loop{
                          .enabled = sample->loops,
                          .start = sample->loopStartFrames,
                          .length = sample->loopLengthFrames,
                      },
                  .pan = output.position,
                  .attenuationDb = output.attenuationDb,
              },
          .vlIndex = reader.u8At(regionOffset + 29),
          .totalLevel = reader.u8At(regionOffset + 15),
          .modulation = regionModulation(reader, bank, version, regionOffset),
      };
      instrument.regions.push_back(std::move(parsed));
    }
    instruments.push_back(std::move(instrument));
  }
  return instruments;
}

}  // namespace

std::optional<ScanSoundBankDraft> addSegSatBank(ScanResultBuilder& builder, const SegSatBankLayout& layout,
                                                SegSatDriverVersion version, SegSatVolumeModel volumeModel,
                                                u8 exportBank) {
  const ByteReader reader = builder.reader();
  auto parsed = parseInstruments(reader, layout, version);
  if (parsed.empty()) {
    return std::nullopt;
  }

  std::map<u32, ParsedRegion*> uniqueSamples;
  for (auto& instrument : parsed) {
    for (auto& region : instrument.regions) {
      uniqueSamples.try_emplace(region.sample.offset, &region);
    }
  }
  if (uniqueSamples.empty()) {
    return std::nullopt;
  }
  SegSatVelocityBank velocityBank =
      readSegSatVelocityBank(reader, layout, layout.sourceBank.value_or(exportBank), volumeModel);

  auto instruments = builder.soundBank(fmt::format("SegSat Sound Bank {}", exportBank));
  auto& samples = instruments.samples();
  for (const auto& [offset, parsedRegion] : uniqueSamples) {
    const std::string name = fmt::format("Sample 0x{:X}", offset);
    samples
        .add(offset,
             Sample{
                 .name = name,
                 .codec = parsedRegion->sample.pcm16 ? AudioCodec::PcmS16 : AudioCodec::PcmS8,
                 .encodedData = reader.range(offset, parsedRegion->sample.bytes),
                 .sampleRate = 44100,
                 .bitsPerSample = static_cast<u16>(parsedRegion->sample.pcm16 ? 16 : 8),
                 .bigEndian = parsedRegion->sample.pcm16,
                 .reverse = parsedRegion->sample.reverse,
                 .loop =
                     Loop{
                         .enabled = parsedRegion->sample.loops,
                         .start = parsedRegion->sample.loopStartFrames,
                         .length = parsedRegion->sample.loopLengthFrames,
                     },
             })
        .source(name, reader.range(offset, parsedRegion->sample.bytes), "segsat-sample");
  }

  const SourceRange headerRange = reader.range(
      layout.offset,
      std::min<u32>(layout.instrumentDataEnd - layout.offset, static_cast<u32>(reader.size() - layout.offset)));
  instruments.include(headerRange);
  instruments.source(SourceRole::SoundBank, "SegSat Instrument Bank", headerRange, "segsat-instrument-bank")
      .derived("source_bank", layout.sourceBank.value_or(exportBank))
      .derived("export_bank", exportBank)
      .derived("instrument_count", layout.instrumentCount)
      .derived("velocity_table_count", (layout.pegTables - layout.velocityTables) / 10);

  for (auto& parsedInstrument : parsed) {
    const std::string name = fmt::format("Instrument {}", parsedInstrument.index);
    Instrument instrument{
        .explicitAddress = InstrumentAddress{.bank = exportBank, .program = parsedInstrument.index},
        .identity =
            segSatInstrumentIdentity(layout.sourceBank.value_or(exportBank), static_cast<u8>(parsedInstrument.index)),
        .pitchBendRangeCents = parsedInstrument.pitchBendRangeCents,
        .name = name,
        .range = parsedInstrument.range,
    };

    auto entry = instruments.add(parsedInstrument.index, std::move(instrument));
    entry.source(name, parsedInstrument.range, "segsat-instrument")
        .derived("volume_bias", parsedInstrument.volumeBias, SourceValueDisplay::SignedDecimal);
    for (size_t regionIndex = 0; regionIndex < parsedInstrument.regions.size(); ++regionIndex) {
      auto& parsedRegion = parsedInstrument.regions[regionIndex];
      const auto sample = samples.find(parsedRegion.sample.offset);
      if (!sample) {
        continue;
      }
      if (parsedInstrument.index < velocityBank.instruments.size() &&
          regionIndex < velocityBank.instruments[parsedInstrument.index].regions.size()) {
        parsedRegion.region.attenuationDb += linearAmplitudeToAttenuationDb(
            velocityBank.instruments[parsedInstrument.index].regions[regionIndex].referenceGain);
      }
      parsedRegion.region.modulation = parsedRegion.modulation;
      entry.region(*sample, std::move(parsedRegion.region)).source("Region", parsedRegion.source, "segsat-region");
    }
  }
  instruments.data(std::move(velocityBank));
  return instruments;
}

SegSatVelocityBank readSegSatVelocityBank(ByteReader reader, const SegSatBankLayout& layout, u8 sourceBank,
                                          SegSatVolumeModel volumeModel) {
  SegSatVelocityBank bank{
      .sourceBank = sourceBank,
  };
  const u16 vlCount = static_cast<u16>((layout.pegTables - layout.velocityTables) / 10);
  bank.tables.reserve(vlCount);
  for (u32 index = 0; index < vlCount; ++index) {
    const u32 offset = layout.offset + layout.velocityTables + index * 10;
    bank.tables.push_back(SegSatVlTable{
        .rate0 = reader.u8At(offset),
        .point0 = reader.u8At(offset + 1),
        .level0 = reader.u8At(offset + 2),
        .rate1 = reader.u8At(offset + 3),
        .point1 = reader.u8At(offset + 4),
        .level1 = reader.u8At(offset + 5),
        .rate2 = reader.u8At(offset + 6),
        .point2 = reader.u8At(offset + 7),
        .level2 = reader.u8At(offset + 8),
        .rate3 = reader.u8At(offset + 9),
    });
  }

  bank.instruments.reserve(layout.instrumentCount);
  for (u32 index = 0; index < layout.instrumentCount; ++index) {
    const u32 instrumentOffset = layout.offset + reader.be16(layout.offset + 8 + index * 2);
    const u32 regionCount = segSatRegionCount(reader.u8At(instrumentOffset + 2));
    SegSatVelocityInstrument instrument{
        .volumeBias = static_cast<s8>(reader.u8At(instrumentOffset + 3)),
    };
    instrument.regions.reserve(regionCount);
    for (u32 regionIndex = 0; regionIndex < regionCount; ++regionIndex) {
      const u32 regionOffset = instrumentOffset + 4 + regionIndex * 0x20;
      const u8 keyLow = reader.u8At(regionOffset);
      const u8 keyHigh = reader.u8At(regionOffset + 1);
      if (keyLow == 0xff || keyLow > keyHigh || !readSampleData(reader, layout, regionOffset)) {
        continue;
      }
      const u8 table = reader.u8At(regionOffset + 29);
      const u8 totalLevel = reader.u8At(regionOffset + 15);
      instrument.regions.push_back(SegSatVelocityRegion{
          .keyLow = keyLow,
          .keyHigh = keyHigh,
          .table = table,
          .totalLevel = totalLevel,
          .referenceGain = table < bank.tables.size() ? segSatRegionReferenceGain(volumeModel, bank.tables[table],
                                                                                  totalLevel, instrument.volumeBias)
                                                      : 1.0,
      });
    }
    bank.instruments.push_back(std::move(instrument));
  }
  return bank;
}

}  // namespace vgmtrans::formats::segsat
