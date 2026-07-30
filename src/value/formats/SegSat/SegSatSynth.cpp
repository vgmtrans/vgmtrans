/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SegSat/SegSat.h"

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

struct ParsedRegion {
  SourceRange range;
  u8 keyLow = 0;
  u8 keyHigh = 127;
  u32 sampleOffset = 0;
  u32 sampleBytes = 0;
  u32 loopStartFrames = 0;
  u32 loopLengthFrames = 0;
  bool loops = false;
  bool reverse = false;
  bool pcm16 = false;
  Region region;
  u8 vlIndex = 0;
  u8 totalLevel = 0;
  std::optional<VibratoSpec> vibrato;
};

struct ParsedInstrument {
  u32 index = 0;
  SourceRange range;
  s8 volumeBias = 0;
  std::vector<ParsedRegion> regions;
};

[[nodiscard]] bool rangeValid(ByteReader reader, u64 offset, u64 size) {
  return offset <= reader.size() && size <= reader.size() - offset;
}

[[nodiscard]] Envelope scspEnvelope(u16 adsr1, u16 adsr2) {
  const u8 attack = static_cast<u8>(adsr1 & 0x1f);
  const u8 decay1 = static_cast<u8>((adsr1 >> 6) & 0x1f);
  const u8 decay2 = static_cast<u8>((adsr1 >> 11) & 0x1f);
  const u8 release = static_cast<u8>(adsr2 & 0x1f);
  const u8 decayLevel = static_cast<u8>((adsr2 >> 5) & 0x1f);

  double decaySeconds = kDecayMilliseconds[decay1 * 2] / 1000.0;
  double sustain = (31 - decayLevel) / 31.0;
  const double decay2Seconds = kDecayMilliseconds[decay2 * 2] / 1000.0;
  if (sustain == 1.0 && decay2Seconds < 1000.0) {
    decaySeconds = decay2Seconds;
    sustain = 0.0;
  } else if (decay2Seconds < 2.0) {
    decaySeconds += decay2Seconds;
    sustain = 0.0;
  }

  // MAME begins SCSP attack at 0x17f rather than silence. The original
  // converter's 0.625 factor retains that audible portion of the envelope.
  return Envelope{
      .attackSeconds = (kAttackMilliseconds[attack * 2] / 1000.0) * 0.625,
      .decaySeconds = decaySeconds,
      .releaseSeconds = kDecayMilliseconds[release * 2] / 1000.0,
      .sustainAmplitude = sustain,
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

[[nodiscard]] std::optional<VibratoSpec> regionVibrato(ByteReader reader, const SegSatBankLayout& bank,
                                                       SegSatDriverVersion version, u32 offset) {
  const bool usesPlfo = (reader.u8At(offset + 2) & 0x40) != 0;
  if (usesPlfo) {
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
        // A zero driver delay means "start now." Omitting it avoids lowering
        // that value to the synth formats' finite one-millisecond floor.
        .delaySeconds = delayRange,
        .depthMode = ModulationDepthMode::Fixed,
    };
  }

  const u8 lfo0 = reader.u8At(offset + 20);
  const u8 lfo1 = reader.u8At(offset + 21);
  const bool modulationDisablesHardwareLfo = (reader.u8At(offset + 14) & 0x80) != 0;
  const u8 waveHigh = lfo0 & 1;
  const u8 waveLow = (lfo1 >> 3) & 3;
  const bool triangle = waveHigh == 0 && waveLow == 2;
  const u8 depth = lfo1 >> 5;
  if (modulationDisablesHardwareLfo || !triangle || depth == 0) {
    return std::nullopt;
  }
  constexpr std::array<double, 32> rates{0.17, 0.19, 0.23, 0.27, 0.34, 0.39, 0.45, 0.55, 0.68, 0.78, 0.92,
                                         1.10, 1.39, 1.60, 1.87, 2.27, 2.87, 3.31, 3.92, 4.79, 6.15, 7.18,
                                         8.60, 10.8, 14.4, 17.2, 21.5, 28.7, 43.1, 57.4, 86.1, 172.3};
  constexpr std::array<double, 8> depths{0.0, 7.0, 13.5, 27.0, 55.0, 112.0, 230.0, 494.0};
  const double rate = rates[(lfo0 >> 2) & 0x1f];
  return VibratoSpec{
      .maxDepthCents = depths[depth],
      .rateHertz = {.minimum = rate, .maximum = rate},
      .depthMode = ModulationDepthMode::Fixed,
  };
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
    };

    for (u32 regionIndex = 0; regionIndex < regionCount; ++regionIndex) {
      const u32 regionOffset = offset + 4 + regionIndex * 0x20;
      const u8 keyLow = reader.u8At(regionOffset);
      const u8 keyHigh = reader.u8At(regionOffset + 1);
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
      const u8 loopMode = static_cast<u8>((reader.u8At(regionOffset + 3) >> 5) & 3);
      if (keyLow == 0xff || keyLow > keyHigh || sampleOffset <= bank.offset || sampleOffset >= bank.offset + 0x7fffe ||
          loopEndFrames < loopStartFrames ||
          !rangeValid(reader, sampleOffset, static_cast<u64>(loopEndFrames) * bytesPerFrame)) {
        continue;
      }

      const s8 fine = static_cast<s8>(reader.u8At(regionOffset + 26));
      const s16 fineCents = static_cast<s16>((fine / 128.0) * 50.0);
      const auto output = directOutput(reader.u8At(regionOffset + 24));
      ParsedRegion parsed{
          .range = reader.range(regionOffset, 0x20),
          .keyLow = keyLow,
          .keyHigh = keyHigh,
          .sampleOffset = sampleOffset,
          .sampleBytes = loopEndFrames * bytesPerFrame,
          .loopStartFrames = loopStartFrames,
          .loopLengthFrames = loopEndFrames - loopStartFrames,
          .loops = loopMode != 0,
          .reverse = loopMode == 2,
          .pcm16 = pcm16,
          .region =
              Region{
                  .keyRange = {.low = keyLow, .high = keyHigh},
                  .range = reader.range(regionOffset, 0x20),
                  .unityKey = reader.u8At(regionOffset + 25) - (fineCents / 100.0),
                  .envelope = scspEnvelope(reader.be16(regionOffset + 10), reader.be16(regionOffset + 12)),
                  .loop =
                      Loop{
                          .enabled = loopMode != 0,
                          .start = loopStartFrames,
                          .length = loopEndFrames - loopStartFrames,
                      },
                  .pan = output.position,
                  .attenuationDb = output.attenuationDb,
              },
          .vlIndex = reader.u8At(regionOffset + 29),
          .totalLevel = reader.u8At(regionOffset + 15),
          .vibrato = regionVibrato(reader, bank, version, regionOffset),
      };
      instrument.regions.push_back(std::move(parsed));
    }
    instruments.push_back(std::move(instrument));
  }
  return instruments;
}

void appendU16(std::vector<u8>& bytes, u16 value) {
  bytes.push_back(static_cast<u8>(value));
  bytes.push_back(static_cast<u8>(value >> 8));
}

}  // namespace

u8 segSatMidiVelocity(u8 velocity, const SegSatVlTable& table, u8 totalLevel, s8 volumeBias) {
  u8 point = 0;
  u8 base = 0;
  u8 rate = table.rate0;
  if (velocity > table.point0) {
    point = table.point0;
    base = table.level0;
    rate = table.rate1;
    if (velocity > table.point1) {
      point = table.point1;
      base = table.level1;
      rate = table.rate2;
      if (velocity > table.point2) {
        point = table.point2;
        base = table.level2;
        rate = table.rate3;
      }
    }
  }

  const u8 margin = velocity - point;
  const u8 shift = rate >> 4;
  const bool onePointFive = (rate & 8) != 0;
  const u32 steep =
      onePointFive ? (((static_cast<u32>(margin & 0x7f) * 12) << shift) >> 3) : (static_cast<u32>(margin) << shift);
  u8 converted = base;
  switch (rate & 7) {
    case 1:
      converted = static_cast<u8>(converted + steep);
      break;
    case 2:
      converted = static_cast<u8>(converted + margin);
      break;
    case 3:
      converted = static_cast<u8>(converted + (margin >> shift));
      break;
    case 5:
      converted = static_cast<u8>(converted - (margin >> shift));
      break;
    case 6:
      converted = static_cast<u8>(converted - margin);
      break;
    case 7:
      converted = static_cast<u8>(converted - steep);
      break;
    default:
      break;
  }

  // The 68000 routine works in bytes. After wraparound, bit 6 distinguishes
  // positive overflow (0x80..0xbf) from negative underflow (0xc0..0xff).
  // This is the saturation branch at mm8audio.bin 0x3b06.
  if (converted & 0x80) {
    converted = (converted & 0x40) ? 0 : 0x7f;
  }

  const u32 volumeScale = (static_cast<u32>(converted) + 1) * (256 - totalLevel);
  const u8 amplitude = static_cast<u8>((volumeScale * 128 * 4 - 1) >> 16);
  const int biased = std::clamp(static_cast<int>(amplitude) + volumeBias, 0, 255);
  const u8 attenuation = static_cast<u8>(~biased);
  const double midiAmplitude = std::pow(10.0, -(attenuation * 0.37529) / 40.0);
  return static_cast<u8>(std::clamp<long>(std::lround(midiAmplitude * 127.0), 0, 127));
}

std::optional<SegSatScannedBank> addSegSatBank(ScanResultBuilder& builder, const SegSatBankLayout& layout,
                                               SegSatDriverVersion version, u8 exportBank) {
  const ByteReader reader = builder.reader();
  auto parsed = parseInstruments(reader, layout, version);
  if (parsed.empty()) {
    return std::nullopt;
  }

  auto samples = builder.samples();
  std::map<u32, ParsedRegion*> uniqueSamples;
  for (auto& instrument : parsed) {
    for (auto& region : instrument.regions) {
      uniqueSamples.try_emplace(region.sampleOffset, &region);
    }
  }
  for (const auto& [offset, parsedRegion] : uniqueSamples) {
    const std::string name = fmt::format("Sample 0x{:X}", offset);
    samples
        .add(offset,
             Sample{
                 .name = name,
                 .codec = parsedRegion->pcm16 ? AudioCodec::PcmS16 : AudioCodec::PcmS8,
                 .encodedData = reader.range(offset, parsedRegion->sampleBytes),
                 .sampleRate = 44100,
                 .bitsPerSample = static_cast<u16>(parsedRegion->pcm16 ? 16 : 8),
                 .bigEndian = parsedRegion->pcm16,
                 .reverse = parsedRegion->reverse,
                 .loop =
                     Loop{
                         .enabled = parsedRegion->loops,
                         .start = parsedRegion->loopStartFrames,
                         .length = parsedRegion->loopLengthFrames,
                     },
             })
        .source(name, reader.range(offset, parsedRegion->sampleBytes), "segsat-sample");
  }
  if (samples.empty()) {
    return std::nullopt;
  }
  const auto sampleRef =
      builder.sampleCollection(fmt::format("SegSat Bank {} Samples", exportBank), std::move(samples));

  auto instruments = builder.instruments();
  const SourceRange headerRange = reader.range(
      layout.offset,
      std::min<u32>(layout.instrumentDataEnd - layout.offset, static_cast<u32>(reader.size() - layout.offset)));
  instruments.include(headerRange);
  instruments.source(SourceRole::InstrumentSet, "SegSat Instrument Bank", headerRange, "segsat-instrument-bank")
      .derived("source_bank", layout.sourceBank.value_or(exportBank))
      .derived("export_bank", exportBank)
      .derived("instrument_count", layout.instrumentCount)
      .derived("velocity_table_count", (layout.pegTables - layout.velocityTables) / 10);

  for (auto& parsedInstrument : parsed) {
    Instrument instrument{
        .explicitAddress = InstrumentAddress{.bank = exportBank, .program = parsedInstrument.index},
        .identity = segSatInstrumentIdentity(layout.sourceBank.value_or(exportBank),
                                             static_cast<u8>(parsedInstrument.index)),
        .name = fmt::format("Instrument {}", parsedInstrument.index),
        .range = parsedInstrument.range,
    };

    auto entry = instruments.add(parsedInstrument.index, std::move(instrument));
    entry.source(entry.value().name, parsedInstrument.range, "segsat-instrument")
        .derived("volume_bias", parsedInstrument.volumeBias, SourceValueDisplay::SignedDecimal);
    for (auto& parsedRegion : parsedInstrument.regions) {
      const auto sample = builder.sampleByKey(sampleRef, parsedRegion.sampleOffset);
      if (!sample) {
        continue;
      }
      parsedRegion.region.modulation.vibrato = parsedRegion.vibrato;
      entry.region(*sample, std::move(parsedRegion.region))
          .source("Region", parsedRegion.range, "segsat-region")
          .derived("velocity_table", parsedRegion.vlIndex)
          .derived("total_level", parsedRegion.totalLevel);
    }
  }
  if (instruments.empty()) {
    return std::nullopt;
  }
  const auto instrumentRef =
      builder.instrumentSet(fmt::format("SegSat Bank {} Instruments", exportBank), std::move(instruments));
  return SegSatScannedBank{
      .instruments = instrumentRef,
      .samples = sampleRef,
      .layout = layout,
  };
}

std::vector<u8> makeSegSatVelocityContext(ByteReader reader, const std::vector<SegSatBankBinding>& banks) {
  std::vector<u8> bytes{'S', 'V', 'L', '2'};
  appendU16(bytes, static_cast<u16>(std::min<size_t>(banks.size(), std::numeric_limits<u16>::max())));
  for (const auto& binding : banks) {
    // Only key ranges, VL indices, total levels, and voice volume bias are
    // serialized below. V2_08 is the MM8-compatible model used by this port;
    // the version-sensitive LFO result from this parse is deliberately unused.
    const auto parsed = parseInstruments(reader, binding.layout, SegSatDriverVersion::V2_08);
    const u16 vlCount = static_cast<u16>((binding.layout.pegTables - binding.layout.velocityTables) / 10);
    bytes.push_back(binding.sourceBank);
    bytes.push_back(binding.exportBank);
    appendU16(bytes, vlCount);
    for (u32 index = 0; index < vlCount; ++index) {
      const u32 offset = binding.layout.offset + binding.layout.velocityTables + index * 10;
      for (u32 byte = 0; byte < 10; ++byte) {
        bytes.push_back(reader.u8At(offset + byte));
      }
    }
    appendU16(bytes, static_cast<u16>(parsed.size()));
    for (const auto& instrument : parsed) {
      bytes.push_back(static_cast<u8>(instrument.volumeBias));
      bytes.push_back(static_cast<u8>(std::min<size_t>(instrument.regions.size(), 255)));
      for (const auto& region : instrument.regions) {
        bytes.push_back(region.keyLow);
        bytes.push_back(region.keyHigh);
        bytes.push_back(region.vlIndex);
        bytes.push_back(region.totalLevel);
      }
    }
  }
  return bytes;
}

}  // namespace vgmtrans::formats::segsat
