/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS2/SonyPS2.h"

#include "value/synth/PsxSpu.h"
#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace vgmtrans::formats::sony_ps2 {

using namespace core;

namespace {

constexpr u32 kProgramBytes = 36;
constexpr u32 kSplitBytes = 20;
constexpr u32 kSampleBytes = 42;
constexpr u32 kMaxDerivedRegions = 32768;

struct Chunk {
  u32 offset = 0;
  u32 size = 0;
  std::vector<std::optional<u32>> entries;
};

struct ProgramParam {
  u32 offset = 0;
  u32 splitOffset = 0;
  u8 splitCount = 0;
  u8 volume = 0;
  s8 pan = 0;
  s8 transpose = 0;
  s8 detune = 0;
  s8 keyFollowPan = 0;
  u8 keyFollowPanCenter = 60;
  u8 attributes = 0;
  u8 pitchWave = 0;
  u8 ampWave = 0;
  u8 pitchPhase = 0;
  u8 ampPhase = 0;
  u8 pitchRandomPhase = 0;
  u8 ampRandomPhase = 0;
  u16 pitchCycle = 0;
  u16 ampCycle = 0;
  s16 pitchPositive = 0;
  s16 pitchNegative = 0;
  s16 midiPitchPositive = 0;
  s16 midiPitchNegative = 0;
  s8 ampPositive = 0;
  s8 ampNegative = 0;
  s8 midiAmpPositive = 0;
  s8 midiAmpNegative = 0;
};

struct SplitParam {
  u32 offset = 0;
  u16 sampleSet = 0xffff;
  u8 low = 0;
  u8 cross = 0;
  u8 high = 127;
  u16 bendLow = 256;
  u16 bendHigh = 256;
  s8 keyFollowPitch = 0;
  u8 keyFollowPitchCenter = 60;
  s8 keyFollowAmp = 0;
  u8 keyFollowAmpCenter = 60;
  s8 keyFollowPan = 0;
  u8 keyFollowPanCenter = 60;
  u8 volume = 0;
  s8 pan = 0;
  s8 transpose = 0;
  s8 detune = 0;
};

struct SampleParam {
  u32 offset = 0;
  u16 vag = 0xffff;
  u8 low = 0;
  u8 cross = 0;
  u8 high = 127;
  s8 velocityPitch = 0;
  u8 velocityPitchCenter = 64;
  u8 velocityPitchCurve = 0;
  s8 velocityAmp = 0;
  u8 velocityAmpCenter = 64;
  u8 velocityAmpCurve = 0;
  u8 baseNote = 60;
  s8 detune = 0;
  s8 pan = 0;
  u8 group = 0;
  u8 priority = 0;
  u8 volume = 0;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  s8 followAttack = 0;
  u8 centerAttack = 60;
  s8 followDecay = 0;
  u8 centerDecay = 60;
  s8 followSustainRate = 0;
  u8 centerSustainRate = 60;
  s8 followRelease = 0;
  u8 centerRelease = 60;
  s8 followSustainLevel = 0;
  u8 centerSustainLevel = 60;
  u16 pitchDelay = 0;
  u16 pitchFade = 0;
  u16 ampDelay = 0;
  u16 ampFade = 0;
  u8 lfoAttributes = 0;
  u8 routing = 0;
};

[[nodiscard]] bool textAt(ByteReader reader, u32 offset, std::string_view text) {
  return reader.has(offset, text.size()) && std::ranges::equal(text, reader.slice(offset, text.size()));
}

[[nodiscard]] std::optional<Chunk> chunkAt(ByteReader reader, u32 fileOffset, u32 fileEnd, u32 relative,
                                           std::string_view tag) {
  if (relative == 0xffffffff || relative > fileEnd - fileOffset) {
    return std::nullopt;
  }
  const u32 offset = fileOffset + relative;
  if (!reader.has(offset, 16) || !textAt(reader, offset, tag)) {
    return std::nullopt;
  }
  const u32 size = reader.le32(offset + 8);
  const u32 maximum = reader.le32(offset + 12);
  if (size < 16 || size > fileEnd - offset || maximum > 65535 || static_cast<u64>(maximum + 1) * 4 > size - 16) {
    return std::nullopt;
  }
  Chunk result{.offset = offset, .size = size};
  result.entries.reserve(maximum + 1);
  for (u32 index = 0; index <= maximum; ++index) {
    const u32 pointer = reader.le32(offset + 16 + index * 4);
    if (pointer == 0xffffffff) {
      result.entries.push_back(std::nullopt);
    } else if (pointer < size) {
      result.entries.push_back(offset + pointer);
    } else {
      return std::nullopt;
    }
  }
  return result;
}

[[nodiscard]] ProgramParam readProgram(ByteReader reader, u32 offset) {
  return ProgramParam{
      .offset = offset,
      .splitOffset = offset + reader.le32(offset),
      .splitCount = reader.u8At(offset + 4),
      .volume = reader.u8At(offset + 6),
      .pan = static_cast<s8>(reader.u8At(offset + 7)),
      .transpose = static_cast<s8>(reader.u8At(offset + 8)),
      .detune = static_cast<s8>(reader.u8At(offset + 9)),
      .keyFollowPan = static_cast<s8>(reader.u8At(offset + 10)),
      .keyFollowPanCenter = reader.u8At(offset + 11),
      .attributes = reader.u8At(offset + 12),
      .pitchWave = reader.u8At(offset + 14),
      .ampWave = reader.u8At(offset + 15),
      .pitchPhase = reader.u8At(offset + 16),
      .ampPhase = reader.u8At(offset + 17),
      .pitchRandomPhase = reader.u8At(offset + 18),
      .ampRandomPhase = reader.u8At(offset + 19),
      .pitchCycle = reader.le16(offset + 20),
      .ampCycle = reader.le16(offset + 22),
      .pitchPositive = static_cast<s16>(reader.le16(offset + 24)),
      .pitchNegative = static_cast<s16>(reader.le16(offset + 26)),
      .midiPitchPositive = static_cast<s16>(reader.le16(offset + 28)),
      .midiPitchNegative = static_cast<s16>(reader.le16(offset + 30)),
      .ampPositive = static_cast<s8>(reader.u8At(offset + 32)),
      .ampNegative = static_cast<s8>(reader.u8At(offset + 33)),
      .midiAmpPositive = static_cast<s8>(reader.u8At(offset + 34)),
      .midiAmpNegative = static_cast<s8>(reader.u8At(offset + 35)),
  };
}

[[nodiscard]] SplitParam readSplit(ByteReader reader, u32 offset) {
  return SplitParam{
      .offset = offset,
      .sampleSet = reader.le16(offset),
      .low = reader.u8At(offset + 2),
      .cross = reader.u8At(offset + 3),
      .high = reader.u8At(offset + 4),
      .bendLow = reader.le16(offset + 6),
      .bendHigh = reader.le16(offset + 8),
      .keyFollowPitch = static_cast<s8>(reader.u8At(offset + 10)),
      .keyFollowPitchCenter = reader.u8At(offset + 11),
      .keyFollowAmp = static_cast<s8>(reader.u8At(offset + 12)),
      .keyFollowAmpCenter = reader.u8At(offset + 13),
      .keyFollowPan = static_cast<s8>(reader.u8At(offset + 14)),
      .keyFollowPanCenter = reader.u8At(offset + 15),
      .volume = reader.u8At(offset + 16),
      .pan = static_cast<s8>(reader.u8At(offset + 17)),
      .transpose = static_cast<s8>(reader.u8At(offset + 18)),
      .detune = static_cast<s8>(reader.u8At(offset + 19)),
  };
}

[[nodiscard]] SampleParam readSample(ByteReader reader, u32 offset) {
  return SampleParam{
      .offset = offset,
      .vag = reader.le16(offset),
      .low = reader.u8At(offset + 2),
      .cross = reader.u8At(offset + 3),
      .high = reader.u8At(offset + 4),
      .velocityPitch = static_cast<s8>(reader.u8At(offset + 5)),
      .velocityPitchCenter = reader.u8At(offset + 6),
      .velocityPitchCurve = reader.u8At(offset + 7),
      .velocityAmp = static_cast<s8>(reader.u8At(offset + 8)),
      .velocityAmpCenter = reader.u8At(offset + 9),
      .velocityAmpCurve = reader.u8At(offset + 10),
      .baseNote = reader.u8At(offset + 11),
      .detune = static_cast<s8>(reader.u8At(offset + 12)),
      .pan = static_cast<s8>(reader.u8At(offset + 13)),
      .group = reader.u8At(offset + 14),
      .priority = reader.u8At(offset + 15),
      .volume = reader.u8At(offset + 16),
      .adsr1 = reader.le16(offset + 18),
      .adsr2 = reader.le16(offset + 20),
      .followAttack = static_cast<s8>(reader.u8At(offset + 22)),
      .centerAttack = reader.u8At(offset + 23),
      .followDecay = static_cast<s8>(reader.u8At(offset + 24)),
      .centerDecay = reader.u8At(offset + 25),
      .followSustainRate = static_cast<s8>(reader.u8At(offset + 26)),
      .centerSustainRate = reader.u8At(offset + 27),
      .followRelease = static_cast<s8>(reader.u8At(offset + 28)),
      .centerRelease = reader.u8At(offset + 29),
      .followSustainLevel = static_cast<s8>(reader.u8At(offset + 30)),
      .centerSustainLevel = reader.u8At(offset + 31),
      .pitchDelay = reader.le16(offset + 32),
      .pitchFade = reader.le16(offset + 34),
      .ampDelay = reader.le16(offset + 36),
      .ampFade = reader.le16(offset + 38),
      .lfoAttributes = reader.u8At(offset + 40),
      .routing = reader.u8At(offset + 41),
  };
}

[[nodiscard]] int adjustedField(int original, int key, s8 follow, u8 center, int maximum) {
  return std::clamp(original + ((key - static_cast<int>(center)) * static_cast<int>(follow)) / 12, 0, maximum);
}

[[nodiscard]] Envelope keyFollowEnvelope(const SampleParam& sample, int key) {
  u16 adsr1 = sample.adsr1;
  u16 adsr2 = sample.adsr2;
  const int attack = adjustedField((adsr1 >> 8) & 0x7f, key, sample.followAttack, sample.centerAttack, 0x7f);
  const int decay = adjustedField((adsr1 >> 4) & 0x0f, key, sample.followDecay, sample.centerDecay, 0x0f);
  const int sustainLevel = adjustedField(adsr1 & 0x0f, key, sample.followSustainLevel, sample.centerSustainLevel, 0x0f);
  const int sustainRate =
      adjustedField((adsr2 >> 6) & 0x7f, key, sample.followSustainRate, sample.centerSustainRate, 0x7f);
  const int release = adjustedField(adsr2 & 0x1f, key, sample.followRelease, sample.centerRelease, 0x1f);
  adsr1 = static_cast<u16>((adsr1 & 0x8000) | (attack << 8) | (decay << 4) | sustainLevel);
  adsr2 = static_cast<u16>((adsr2 & 0xc020) | (sustainRate << 6) | release);
  return psxSpuEnvelope(adsr1, adsr2, PsxSpuGeneration::Ps2);
}

[[nodiscard]] int convexVelocity(int velocity) {
  const int square = velocity * velocity;
  const int quotient = square / 127;
  return std::max(1, (quotient + (square - quotient) / 2) >> 6);
}

[[nodiscard]] int velocityCurve(u8 type, int velocity) {
  velocity = std::clamp(velocity, 1, 127);
  switch (type) {
    case 0:
      return velocity;
    case 1:
      return 128 - velocity;
    case 2:
      return convexVelocity(velocity);
    case 3:
      return 128 - convexVelocity(velocity);
    case 4:
      return 128 - convexVelocity(128 - velocity);
    case 5:
      return convexVelocity(128 - velocity);
    default:
      return velocity;
  }
}

[[nodiscard]] double velocityCurveCorrection(u8 curve, int rawVelocity, u8 targetVelocity) {
  const double targetGain = LevelScale::linearFromMidi7(targetVelocity);
  return targetGain == 0.0 ? 1.0 : velocityGain(static_cast<u8>(velocityCurve(curve, rawVelocity))) / targetGain;
}

[[nodiscard]] double bipolarVelocityCurve(u8 type, int velocity, int center) {
  const double normalized = (std::clamp(velocity, 1, 127) - 1) / 126.0;
  const double normalizedCenter = (std::clamp(center, 1, 127) - 1) / 126.0;
  const double linear = normalized - normalizedCenter;
  switch (type) {
    case 0:
      return linear;
    case 1:
      return -linear;
    case 2:
      return normalized * normalized - normalizedCenter * normalizedCenter;
    case 3:
      return normalizedCenter * normalizedCenter - normalized * normalized;
    case 4:
      return (4.0 * normalized - normalized * normalized) -
             (4.0 * normalizedCenter - normalizedCenter * normalizedCenter);
    case 5:
      return (4.0 * normalizedCenter - normalizedCenter * normalizedCenter) -
             (4.0 * normalized - normalized * normalized);
    case 6:
    case 7:
    case 8:
    case 9: {
      const int difference = velocity - center;
      if (difference == 0) {
        return 0.0;
      }
      const double denominator = difference > 0 ? std::max(127 - center, 1) : std::max(center - 1, 1);
      const double magnitude = std::abs(difference) / denominator;
      double shaped = magnitude;
      if (type == 8) {
        shaped *= shaped;
      } else if (type == 9) {
        shaped = 1.0 - (1.0 - shaped) * (1.0 - shaped);
      }
      const double signedValue = std::copysign(shaped, static_cast<double>(difference));
      return type == 7 ? -signedValue : signedValue;
    }
    default:
      return linear;
  }
}

[[nodiscard]] double crossfade(u8 rawLow, u8 cross, u8 rawHigh, int value) {
  const int low = rawLow & 0x7f;
  const int high = rawHigh & 0x7f;
  if (value < low || value > high) {
    return 0.0;
  }
  double gain = 1.0;
  if ((rawLow & 0x80) != 0 && cross != low && value < cross) {
    gain *= static_cast<double>(value - (low - 1)) / (cross - (low - 1));
  }
  if ((rawHigh & 0x80) != 0 && high != cross && value >= cross) {
    gain *= static_cast<double>(high + 1 - value) / (high + 1 - cross);
  }
  return std::clamp(gain, 0.0, 1.0);
}

[[nodiscard]] double gainFromRaw(int value) {
  return std::clamp(value, 0, 128) / 128.0;
}

[[nodiscard]] int panMagnitude(s8 value) {
  return std::min(std::abs(static_cast<int>(value)), 127);
}

[[nodiscard]] double attenuation(double gain) {
  return gain <= 0.0 ? 96.0 : -20.0 * std::log10(gain);
}

[[nodiscard]] std::optional<LfoWaveform> waveform(u8 value) {
  switch (value) {
    case 1:
      return LfoWaveform::SawtoothUp;
    case 2:
      return LfoWaveform::SawtoothDown;
    case 3:
      return LfoWaveform::Triangle;
    case 4:
      return LfoWaveform::Square;
    case 5:
      return LfoWaveform::Noise;
    case 6:
      return LfoWaveform::Sine;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] InstrumentModulation modulation(const ProgramParam& program, const SplitParam& split,
                                              const SampleParam& sample, int key, int velocity) {
  InstrumentModulation result;
  const double pitchKey = (key - static_cast<int>(split.keyFollowPitchCenter)) * split.keyFollowPitch / 12.0;
  const double pitchVelocity =
      bipolarVelocityCurve(sample.velocityPitchCurve, velocity, sample.velocityPitchCenter) * sample.velocityPitch;
  const double pitchDepth = std::max(std::abs(program.pitchPositive + pitchKey + pitchVelocity),
                                     std::abs(program.pitchNegative + pitchKey + pitchVelocity)) *
                            100.0 / 128.0;
  if (program.pitchWave != 0 && program.pitchCycle != 0 && pitchDepth != 0.0) {
    result.vibrato = VibratoSpec{
        .maxDepthCents = pitchDepth,
        .rateHertz = ModulationRange{1000.0 / program.pitchCycle, 1000.0 / program.pitchCycle},
        .waveform = waveform(program.pitchWave),
        .delaySeconds = ModulationRange{sample.pitchDelay / 1000.0, sample.pitchDelay / 1000.0},
        .depthMode = ModulationDepthMode::Fixed,
    };
  }
  const double ampKey = (key - static_cast<int>(split.keyFollowAmpCenter)) * split.keyFollowAmp / 12.0;
  const double ampVelocity =
      bipolarVelocityCurve(sample.velocityAmpCurve, velocity, sample.velocityAmpCenter) * sample.velocityAmp;
  const double ampDepth = std::clamp(std::max(std::abs(program.ampPositive + ampKey + ampVelocity),
                                              std::abs(program.ampNegative + ampKey + ampVelocity)) /
                                         128.0,
                                     0.0, 0.999);
  if (program.ampWave != 0 && program.ampCycle != 0 && ampDepth != 0.0) {
    result.tremolo = TremoloSpec{
        .maxDepthDb = -20.0 * std::log10(1.0 - ampDepth),
        .rateHertz = ModulationRange{1000.0 / program.ampCycle, 1000.0 / program.ampCycle},
        .waveform = waveform(program.ampWave),
        .gainMode = TremoloGainMode::BipolarAroundNominal,
        .delaySeconds = ModulationRange{sample.ampDelay / 1000.0, sample.ampDelay / 1000.0},
        .depthMode = ModulationDepthMode::Fixed,
    };
  }
  return result;
}

[[nodiscard]] bool keyDependent(const ProgramParam& program, const SplitParam& split, const SampleParam& sample) {
  return (split.low & 0x80) != 0 || (split.high & 0x80) != 0 || program.keyFollowPan != 0 ||
         split.keyFollowPitch != 0 || split.keyFollowAmp != 0 || split.keyFollowPan != 0 || sample.followAttack != 0 ||
         sample.followDecay != 0 || sample.followSustainRate != 0 || sample.followRelease != 0 ||
         sample.followSustainLevel != 0;
}

[[nodiscard]] bool velocityDependent(const SampleParam& sample) {
  return (sample.low & 0x80) != 0 || (sample.high & 0x80) != 0 || sample.velocityPitch != 0 ||
         sample.velocityAmp != 0 || sample.velocityAmpCurve != 0;
}

}  // namespace

void addSoundBank(ScanResultBuilder& result, u32 offset, SoundBankData layout) {
  const ByteReader reader = result.reader();
  const u32 headerBytes = reader.le32(offset + 0x1c);
  const u32 end = offset + headerBytes;
  const auto programs = chunkAt(reader, offset, end, reader.le32(offset + 0x24), "IECSgorP");
  const auto sampleSets = chunkAt(reader, offset, end, reader.le32(offset + 0x28), "IECStesS");
  const auto samples = chunkAt(reader, offset, end, reader.le32(offset + 0x2c), "IECSlpmS");
  const auto setb = chunkAt(reader, offset, end, reader.le32(offset + 0x34), "IECSbteS");
  if (!programs || !sampleSets || !samples) {
    result.warning("SonyPS2 HD is missing its Prog, Sset, or Smpl chunk", reader.range(offset, headerBytes));
    return;
  }

  auto bank = result.soundBank(fmt::format("{} HD", result.sourceDisplayName()), reader.range(offset, headerBytes));
  auto& instruments = bank.instruments();
  instruments.include(reader.range(offset, headerBytes));
  instruments.source(SourceRole::Header, "SonyPS2 HD header", reader.range(offset, 0x40), "sony-ps2-hd-header")
      .fieldsAsChildren()
      .field("header_size", reader.range(offset + 0x1c, 4), headerBytes)
      .field("body_size", reader.range(offset + 0x20, 4), layout.expectedBodyBytes)
      .field("program_chunk", reader.range(offset + 0x24, 4), reader.le32(offset + 0x24), SourceValueDisplay::Address)
      .field("sample_set_chunk", reader.range(offset + 0x28, 4), reader.le32(offset + 0x28),
             SourceValueDisplay::Address)
      .field("sample_chunk", reader.range(offset + 0x2c, 4), reader.le32(offset + 0x2c), SourceValueDisplay::Address)
      .field("vag_info_chunk", reader.range(offset + 0x30, 4), reader.le32(offset + 0x30), SourceValueDisplay::Address)
      .field("setb_chunk", reader.range(offset + 0x34, 4), reader.le32(offset + 0x34), SourceValueDisplay::Address);

  bool warnedLfoShape = false;
  bool warnedLfoPhase = false;
  bool warnedLfoAsymmetry = false;
  bool warnedBendRange = false;
  bool warnedPanPhase = false;
  bool warnedNoise = false;
  bool warnedRegionLimit = false;
  bool warnedVoicePolicy = false;
  bool warnedRouting = false;
  for (u32 programIndex = 0; programIndex < programs->entries.size(); ++programIndex) {
    const auto programOffset = programs->entries[programIndex];
    if (!programOffset || !reader.has(*programOffset, kProgramBytes)) {
      continue;
    }
    const ProgramParam program = readProgram(reader, *programOffset);
    if (program.splitCount == 0 || reader.u8At(*programOffset + 5) != kSplitBytes ||
        !reader.has(program.splitOffset, static_cast<u64>(program.splitCount) * kSplitBytes)) {
      continue;
    }
    ProgramRuntimeInfo runtime{
        .program = static_cast<u8>(programIndex),
        .pitchDepthPositive = program.pitchPositive,
        .pitchDepthNegative = program.pitchNegative,
        .midiPitchDepthPositive = program.midiPitchPositive,
        .midiPitchDepthNegative = program.midiPitchNegative,
        .pitchBendPositive = 0,
        .pitchBendNegative = 0,
        .ampDepthPositive = program.ampPositive,
        .ampDepthNegative = program.ampNegative,
        .midiAmpDepthPositive = program.midiAmpPositive,
        .midiAmpDepthNegative = program.midiAmpNegative,
    };
    std::vector<SplitParam> splits;
    splits.reserve(program.splitCount);
    for (u32 index = 0; index < program.splitCount; ++index) {
      const SplitParam split = readSplit(reader, program.splitOffset + index * kSplitBytes);
      runtime.pitchBendNegative = std::max(runtime.pitchBendNegative, split.bendLow);
      runtime.pitchBendPositive = std::max(runtime.pitchBendPositive, split.bendHigh);
      runtime.pitchBendZones.push_back(PitchBendZone{
          .keyLow = static_cast<u8>(split.low & 0x7f),
          .keyHigh = static_cast<u8>(split.high & 0x7f),
          .negative = split.bendLow,
          .positive = split.bendHigh,
      });
      splits.push_back(split);
    }
    if (!warnedBendRange &&
        (std::ranges::any_of(splits, [](const SplitParam& split) { return split.bendLow != split.bendHigh; }) ||
         std::ranges::any_of(splits, [&](const SplitParam& split) {
           return split.bendLow != splits.front().bendLow || split.bendHigh != splits.front().bendHigh;
         }))) {
      // Sequence playback retains the driver's physical, direction-specific
      // result for each active split. The static Instrument range is only the
      // symmetric channel capacity used for MIDI and live input; simultaneous
      // voices with different ranges cannot be expressed on one MIDI channel.
      instruments.warning("SonyPS2 split- or direction-specific pitch bends use one live-input channel range",
                          reader.range(program.offset, kProgramBytes));
      warnedBendRange = true;
    }
    if (!warnedLfoAsymmetry &&
        (std::abs(static_cast<int>(program.pitchPositive)) != std::abs(static_cast<int>(program.pitchNegative)) ||
         std::abs(static_cast<int>(program.midiPitchPositive)) !=
             std::abs(static_cast<int>(program.midiPitchNegative)) ||
         std::abs(static_cast<int>(program.ampPositive)) != std::abs(static_cast<int>(program.ampNegative)) ||
         std::abs(static_cast<int>(program.midiAmpPositive)) != std::abs(static_cast<int>(program.midiAmpNegative)))) {
      // Value-core modulation depths are symmetric. Preserve the maximum
      // physical excursion instead of silently discarding one polarity.
      instruments.warning("SonyPS2 asymmetric LFO depth is represented by its larger physical excursion",
                          reader.range(program.offset, kProgramBytes));
      warnedLfoAsymmetry = true;
    }
    const bool programWet = std::ranges::any_of(splits, [&](const SplitParam& split) {
      if (split.sampleSet >= sampleSets->entries.size() || !sampleSets->entries[split.sampleSet]) {
        return false;
      }
      const u32 sampleSetOffset = *sampleSets->entries[split.sampleSet];
      if (!reader.has(sampleSetOffset, 4)) {
        return false;
      }
      const u8 count = reader.u8At(sampleSetOffset + 3);
      if (!reader.has(sampleSetOffset + 4, static_cast<u64>(count) * 2)) {
        return false;
      }
      for (u32 number = 0; number < count; ++number) {
        const u16 index = reader.le16(sampleSetOffset + 4 + number * 2);
        if (index < samples->entries.size() && samples->entries[index] &&
            reader.has(*samples->entries[index], kSampleBytes) &&
            (reader.u8At(*samples->entries[index] + 41) & 0x0c) != 0) {
          return true;
        }
      }
      return false;
    });
    layout.runtimePrograms.push_back(runtime);
    auto instrument = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = programIndex},
        .identity = instrumentIdentity(0, static_cast<u8>(programIndex)),
        .pitchBendRangeCents = static_cast<u16>(std::min<u32>(
            65535, (std::max(runtime.pitchBendPositive, runtime.pitchBendNegative) * 100u + 127u) / 128u)),
        .reverb = programWet ? 1.0 : 0.0,
        .name = fmt::format("Program {}", programIndex),
        .range = reader.range(*programOffset, kProgramBytes),
    });
    instrument.source(instrument.value().name, reader.range(*programOffset, kProgramBytes), "sony-ps2-program");

    u32 derivedRegions = 0;
    for (const auto& split : splits) {
      if (split.sampleSet >= sampleSets->entries.size() || !sampleSets->entries[split.sampleSet]) {
        continue;
      }
      const u32 sampleSetOffset = *sampleSets->entries[split.sampleSet];
      if (!reader.has(sampleSetOffset, 4)) {
        continue;
      }
      const u8 velocityCurveType = reader.u8At(sampleSetOffset);
      const int sampleSetVelocityLow = std::clamp<int>(reader.u8At(sampleSetOffset + 1) & 0x7f, 1, 127);
      const int sampleSetVelocityHigh = std::clamp<int>(reader.u8At(sampleSetOffset + 2) & 0x7f, 1, 127);
      const u8 sampleCount = reader.u8At(sampleSetOffset + 3);
      if (!reader.has(sampleSetOffset + 4, static_cast<u64>(sampleCount) * 2)) {
        continue;
      }
      for (u32 sampleNumber = 0; sampleNumber < sampleCount; ++sampleNumber) {
        const u16 sampleIndex = reader.le16(sampleSetOffset + 4 + sampleNumber * 2);
        if (sampleIndex >= samples->entries.size() || !samples->entries[sampleIndex] ||
            !reader.has(*samples->entries[sampleIndex], kSampleBytes)) {
          continue;
        }
        const SampleParam sample = readSample(reader, *samples->entries[sampleIndex]);
        if ((sample.group != 0 || sample.priority != 0) && !warnedVoicePolicy) {
          // These fields control admission and stealing of live SPU2 voices;
          // they do not alter a region's static synthesis parameters.
          instruments.warning("SonyPS2 voice groups and priorities have no synth-model equivalent",
                              reader.range(sample.offset, kSampleBytes));
          warnedVoicePolicy = true;
        }
        if (sample.routing != 0 && !warnedRouting) {
          // Preserve effect-send capability as Instrument::reverb. The exact
          // dry/wet left/right matrix and SPU2 core selection need a routing model.
          instruments.warning("SonyPS2 SPU2 core and dry/wet routing is reduced to generic reverb capability",
                              reader.range(sample.offset, kSampleBytes));
          warnedRouting = true;
        }
        if (sample.pan < 0 && !warnedPanPhase) {
          instruments.warning("SonyPS2 negative-phase pan is reduced to ordinary stereo position",
                              reader.range(sample.offset, kSampleBytes));
          warnedPanPhase = true;
        }
        if (sample.vag == 0xffff) {
          if (!warnedNoise) {
            instruments.warning("SonyPS2 noise-generator regions cannot be represented as sampled regions",
                                reader.range(sample.offset, kSampleBytes));
            warnedNoise = true;
          }
          continue;
        }
        if (sample.vag >= layout.vags.size() || !layout.vags[sample.vag]) {
          continue;
        }
        const VagInfo& vag = *layout.vags[sample.vag];
        const int keyLow = std::min<int>(split.low & 0x7f, 127);
        const int keyHigh = std::min<int>(split.high & 0x7f, 127);
        const int rawVelocityLow = std::max<int>(sampleSetVelocityLow, sample.low & 0x7f);
        const int rawVelocityHigh = std::min<int>(sampleSetVelocityHigh, sample.high & 0x7f);
        if (rawVelocityLow > rawVelocityHigh) {
          continue;
        }
        const int targetVelocityLow = midiVelocity(static_cast<u8>(rawVelocityLow));
        const int targetVelocityHigh = midiVelocity(static_cast<u8>(rawVelocityHigh));
        const bool splitKeys = keyDependent(program, split, sample);
        const bool splitVelocities = velocityDependent(sample) || velocityCurveType != 0;
        const u32 keyZones = splitKeys ? std::max(0, keyHigh - keyLow + 1) : 1;
        u32 velocityStep = 1;
        const u32 velocityZones = splitVelocities ? std::max(0, targetVelocityHigh - targetVelocityLow + 1) : 1;
        if (keyZones != 0 && derivedRegions + keyZones * velocityZones > kMaxDerivedRegions) {
          velocityStep = 4;
          if (!warnedRegionLimit) {
            instruments.warning("SonyPS2 key/velocity modulation required more than 32768 exact zones; velocity was "
                                "quantized to four-step zones",
                                reader.range(sample.offset, kSampleBytes));
            warnedRegionLimit = true;
          }
        }
        for (int key = keyLow; key <= keyHigh;) {
          const int representedKey = splitKeys ? key : std::clamp<int>(split.keyFollowPitchCenter, keyLow, keyHigh);
          const int emittedKeyHigh = splitKeys ? key : keyHigh;
          for (int targetVelocity = targetVelocityLow; targetVelocity <= targetVelocityHigh;) {
            const int representedRawVelocity =
                splitVelocities ? std::clamp<int>(rawVelocityFromMidi(static_cast<u8>(targetVelocity)), rawVelocityLow,
                                                  rawVelocityHigh)
                                : std::clamp<int>(sample.velocityAmpCenter, rawVelocityLow, rawVelocityHigh);
            const int representedTargetVelocity =
                splitVelocities ? targetVelocity : midiVelocity(static_cast<u8>(representedRawVelocity));
            const int emittedVelocityHigh =
                splitVelocities ? std::min(targetVelocity + static_cast<int>(velocityStep) - 1, targetVelocityHigh)
                                : targetVelocityHigh;
            const double keyGain = crossfade(split.low, split.cross, split.high, representedKey);
            const double velocityFade = crossfade(sample.low, sample.cross, sample.high, representedRawVelocity);
            // Region ranges are selected by target MIDI velocity, while all
            // driver curves are evaluated in Sony's linear source domain.
            const double velocityCorrection = velocityCurveCorrection(velocityCurveType, representedRawVelocity,
                                                                      static_cast<u8>(representedTargetVelocity));
            const double gain = gainFromRaw(program.volume) * gainFromRaw(split.volume) * gainFromRaw(sample.volume) *
                                keyGain * velocityFade * velocityCorrection;
            const double panFollow =
                (representedKey - static_cast<int>(program.keyFollowPanCenter)) * program.keyFollowPan / 12.0 +
                (representedKey - static_cast<int>(split.keyFollowPanCenter)) * split.keyFollowPan / 12.0;
            const int rawPan = std::clamp<int>(panMagnitude(program.pan) + panMagnitude(split.pan) +
                                                   panMagnitude(sample.pan) - 128 + static_cast<int>(panFollow),
                                               0, 127);
            Region region{
                .keyRange = KeyRange{static_cast<u8>(key), static_cast<u8>(emittedKeyHigh)},
                .velocityRange = VelocityRange{static_cast<u8>(targetVelocity), static_cast<u8>(emittedVelocityHigh)},
                .range = reader.range(sample.offset, kSampleBytes),
                .unityKey = sample.baseNote - program.transpose - split.transpose -
                            (program.detune + split.detune + sample.detune) / 128.0 +
                            12.0 * std::log2(48000.0 / vag.sampleRate),
                .envelope = keyFollowEnvelope(sample, representedKey),
                .pan = panPositionFrom7Bit(static_cast<u8>(rawPan)),
                .attenuationDb = attenuation(gain),
                .modulation = modulation(program, split, sample, representedKey, representedRawVelocity),
            };
            instrument.region(SampleRef::unbound(sample.vag), std::move(region))
                .source("Sample region", reader.range(sample.offset, kSampleBytes), "sony-ps2-sample-param");
            ++derivedRegions;
            targetVelocity = emittedVelocityHigh + 1;
          }
          key = emittedKeyHigh + 1;
        }
        if ((program.pitchWave == 0x80 || program.ampWave == 0x80) && !warnedLfoShape) {
          instruments.warning("SonyPS2 user-defined LFO wave tables have no value-core waveform model",
                              reader.range(program.offset, kProgramBytes));
          warnedLfoShape = true;
        }
        if ((program.pitchPhase != 0 || program.ampPhase != 0 || program.pitchRandomPhase != 0 ||
             program.ampRandomPhase != 0 || sample.pitchFade != 0 || sample.ampFade != 0 ||
             sample.lfoAttributes != 0) &&
            !warnedLfoPhase) {
          // The model deliberately retains physical depth/rate/delay while phase,
          // random phase, fade-in, and key-off triggering await a richer LFO type.
          instruments.warning("SonyPS2 LFO phase, random phase, fade, or trigger behavior is not representable",
                              reader.range(sample.offset, kSampleBytes));
          warnedLfoPhase = true;
        }
      }
    }
    if (((program.attributes & 1) != 0 || program.pan < 0 ||
         std::ranges::any_of(
             splits, [](const SplitParam& split) { return split.pan < 0; })) &&
        !warnedPanPhase) {
      // ROUND_PAN and the driver's negative pan values can invert SPU output
      // phase. Region::pan can preserve position but not that phase inversion.
      instruments.warning("SonyPS2 round-pan phase inversion is reduced to ordinary stereo position",
                          reader.range(program.offset, kProgramBytes));
      warnedPanPhase = true;
    }
  }

  if (setb) {
    instruments.source(SourceRole::Table, "SonyPS2 Setb chunk", reader.range(setb->offset, setb->size),
                       "sony-ps2-setb");
    bool warnedSetbVoicePolicy = false;
    for (u32 setIndex = 0; setIndex < setb->entries.size(); ++setIndex) {
      const auto setOffset = setb->entries[setIndex];
      if (!setOffset || !reader.has(*setOffset, 4)) {
        continue;
      }
      const u32 maximumTimbre = reader.le32(*setOffset);
      if (maximumTimbre >= 128 || !reader.has(*setOffset + 4, static_cast<u64>(maximumTimbre + 1) * 4)) {
        continue;
      }
      for (u32 timbreIndex = 0; timbreIndex <= maximumTimbre; ++timbreIndex) {
        const u32 timbreRelative = reader.le32(*setOffset + 4 + timbreIndex * 4);
        if (timbreRelative == 0xffffffff || timbreRelative >= setb->size) {
          continue;
        }
        const u32 timbreOffset = setb->offset + timbreRelative;
        if (!reader.has(timbreOffset, 8)) {
          continue;
        }
        const u32 noteBlockOffset = timbreOffset + reader.le32(timbreOffset);
        const u8 noteBytes = reader.u8At(timbreOffset + 4);
        const u8 noteLow = reader.u8At(timbreOffset + 5);
        const u8 noteHigh = reader.u8At(timbreOffset + 6);
        if (noteBytes < 50 || noteLow > noteHigh ||
            !reader.has(noteBlockOffset, static_cast<u64>(noteHigh - noteLow + 1) * noteBytes)) {
          continue;
        }
        bool wet = false;
        for (u32 note = noteLow; note <= noteHigh; ++note) {
          wet |= (reader.u8At(noteBlockOffset + (note - noteLow) * noteBytes + 13) & 0x0c) != 0;
        }
        auto instrument = instruments.append(Instrument{
            .explicitAddress = InstrumentAddress{.bank = 128 + setIndex, .program = timbreIndex},
            .identity = setbInstrumentIdentity(static_cast<u8>(setIndex), static_cast<u8>(timbreIndex)),
            .reverb = wet ? 1.0 : 0.0,
            .name = fmt::format("SE Set {} Timbre {}", setIndex, timbreIndex),
            .range = reader.range(timbreOffset, 8),
        });
        instrument.source(instrument.value().name, reader.range(timbreOffset, 8), "sony-ps2-setb-timbre");
        for (u32 note = noteLow; note <= noteHigh; ++note) {
          const u32 noteOffset = noteBlockOffset + (note - noteLow) * noteBytes;
          const u16 vagIndex = reader.le16(noteOffset);
          if (vagIndex == 0xffff || vagIndex >= layout.vags.size() || !layout.vags[vagIndex]) {
            continue;
          }
          const VagInfo& vag = *layout.vags[vagIndex];
          const u8 routing = reader.u8At(noteOffset + 13);
          if (routing != 0 && !warnedRouting) {
            instruments.warning("SonyPS2 SPU2 core and dry/wet routing is reduced to generic reverb capability",
                                reader.range(noteOffset, noteBytes));
            warnedRouting = true;
          }
          const u8 curve = reader.u8At(noteOffset + 2);
          const s8 velocityPitch = static_cast<s8>(reader.u8At(noteOffset + 42));
          const s8 velocityAmp = static_cast<s8>(reader.u8At(noteOffset + 43));
          const u8 velocityPitchCenter = reader.u8At(noteOffset + 44);
          const u8 velocityAmpCenter = reader.u8At(noteOffset + 45);
          const u8 velocityPitchCurve = reader.u8At(noteOffset + 46);
          const u8 velocityAmpCurve = reader.u8At(noteOffset + 47);
          ProgramParam lfo{
              .volume = reader.u8At(noteOffset + 3),
              .pan = static_cast<s8>(reader.u8At(noteOffset + 4)),
              .transpose = static_cast<s8>(reader.u8At(noteOffset + 5)),
              .detune = static_cast<s8>(reader.u8At(noteOffset + 6)),
              .pitchWave = reader.u8At(noteOffset + 18),
              .ampWave = reader.u8At(noteOffset + 19),
              .pitchPhase = reader.u8At(noteOffset + 20),
              .ampPhase = reader.u8At(noteOffset + 21),
              .pitchRandomPhase = reader.u8At(noteOffset + 22),
              .ampRandomPhase = reader.u8At(noteOffset + 23),
              .pitchCycle = reader.le16(noteOffset + 24),
              .ampCycle = reader.le16(noteOffset + 26),
              .pitchPositive = static_cast<s16>(reader.le16(noteOffset + 28)),
              .pitchNegative = static_cast<s16>(reader.le16(noteOffset + 30)),
              .ampPositive = static_cast<s8>(reader.u8At(noteOffset + 32)),
              .ampNegative = static_cast<s8>(reader.u8At(noteOffset + 33)),
          };
          SplitParam neutralSplit{.low = static_cast<u8>(note),
                                  .cross = static_cast<u8>(note),
                                  .high = static_cast<u8>(note),
                                  .volume = 128,
                                  .pan = 64};
          SampleParam lfoSample{
              .velocityPitch = velocityPitch,
              .velocityPitchCenter = velocityPitchCenter,
              .velocityPitchCurve = velocityPitchCurve,
              .velocityAmp = velocityAmp,
              .velocityAmpCenter = velocityAmpCenter,
              .velocityAmpCurve = velocityAmpCurve,
              .baseNote = static_cast<u8>(note),
              .adsr1 = reader.le16(noteOffset + 14),
              .adsr2 = reader.le16(noteOffset + 16),
              .pitchDelay = reader.le16(noteOffset + 34),
              .pitchFade = reader.le16(noteOffset + 38),
              .ampDelay = reader.le16(noteOffset + 36),
              .ampFade = reader.le16(noteOffset + 40),
              .lfoAttributes = reader.u8At(noteOffset + 49),
          };
          if ((lfo.pitchWave == 0x80 || lfo.ampWave == 0x80) && !warnedLfoShape) {
            instruments.warning("SonyPS2 user-defined LFO wave tables have no value-core waveform model",
                                reader.range(noteOffset, noteBytes));
            warnedLfoShape = true;
          }
          if ((lfo.pitchPhase != 0 || lfo.ampPhase != 0 || lfo.pitchRandomPhase != 0 || lfo.ampRandomPhase != 0 ||
               lfoSample.pitchFade != 0 || lfoSample.ampFade != 0 || lfoSample.lfoAttributes != 0) &&
              !warnedLfoPhase) {
            instruments.warning("SonyPS2 LFO phase, random phase, fade, or trigger behavior is not representable",
                                reader.range(noteOffset, noteBytes));
            warnedLfoPhase = true;
          }
          if (!warnedLfoAsymmetry &&
              (std::abs(static_cast<int>(lfo.pitchPositive)) != std::abs(static_cast<int>(lfo.pitchNegative)) ||
               std::abs(static_cast<int>(lfo.ampPositive)) != std::abs(static_cast<int>(lfo.ampNegative)))) {
            instruments.warning("SonyPS2 asymmetric LFO depth is represented by its larger physical excursion",
                                reader.range(noteOffset, noteBytes));
            warnedLfoAsymmetry = true;
          }
          const bool velocityZones = curve != 0 || velocityPitch != 0 || velocityAmp != 0;
          const int targetVelocityLow = midiVelocity(1);
          for (int targetVelocity = targetVelocityLow; targetVelocity <= 127;) {
            const int representedRawVelocity =
                velocityZones ? rawVelocityFromMidi(static_cast<u8>(targetVelocity)) : 127;
            const int representedTargetVelocity = velocityZones ? targetVelocity : 127;
            const int emittedHigh = velocityZones ? targetVelocity : 127;
            const double curveCorrection =
                velocityCurveCorrection(curve, representedRawVelocity, static_cast<u8>(representedTargetVelocity));
            const double gain = gainFromRaw(lfo.volume) * curveCorrection;
            Region region{
                .keyRange = KeyRange{static_cast<u8>(note), static_cast<u8>(note)},
                .velocityRange = VelocityRange{static_cast<u8>(targetVelocity), static_cast<u8>(emittedHigh)},
                .range = reader.range(noteOffset, noteBytes),
                .unityKey = note - lfo.transpose - lfo.detune / 128.0 + 12.0 * std::log2(48000.0 / vag.sampleRate),
                .envelope = psxSpuEnvelope(lfoSample.adsr1, lfoSample.adsr2, PsxSpuGeneration::Ps2),
                .pan = panPositionFrom7Bit(static_cast<u8>(panMagnitude(lfo.pan))),
                .attenuationDb = attenuation(gain),
                .modulation = modulation(lfo, neutralSplit, lfoSample, note, representedRawVelocity),
            };
            instrument.region(SampleRef::unbound(vagIndex), std::move(region))
                .source("SE timbre note", reader.range(noteOffset, noteBytes), "sony-ps2-setb-note");
            targetVelocity = emittedHigh + 1;
          }
          if ((reader.u8At(noteOffset + 8) != 0 || reader.u8At(noteOffset + 9) != 0) && !warnedSetbVoicePolicy) {
            // Group limits and priorities affect voice stealing rather than
            // the static sound of an exported region.
            instruments.warning("SonyPS2 Setb group limits and voice priorities have no synth-model equivalent",
                                reader.range(noteOffset, noteBytes));
            warnedSetbVoicePolicy = true;
          }
          if (((reader.u8At(noteOffset + 12) & 1) != 0 || lfo.pan < 0) && !warnedPanPhase) {
            instruments.warning("SonyPS2 round- or negative-phase pan is reduced to ordinary stereo position",
                                reader.range(noteOffset, noteBytes));
            warnedPanPhase = true;
          }
        }
      }
    }
  }
  bank.data(std::move(layout));
}

}  // namespace vgmtrans::formats::sony_ps2
