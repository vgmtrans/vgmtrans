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
#include <bit>
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
// A region needs at least fourteen SoundFont generators, plus any LFO
// generators. Staying below this bank-wide count leaves room in SF2's 16-bit
// generator and modulator indices without changing ordinary banks.
constexpr u32 kMaxSynthRegions = 3000;

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

struct SampleSetParam {
  u32 offset = 0;
  u8 velocityCurve = 0;
  int velocityLow = 1;
  int velocityHigh = 127;
  std::vector<SampleParam> samples;
};

struct ProgramDefinition {
  u32 index = 0;
  ProgramParam program;
  std::vector<SplitParam> splits;
};

struct SetbNoteParam {
  u32 offset = 0;
  u8 size = 0;
  u8 key = 0;
  u8 velocityCurve = 0;
  u8 groupLimit = 0;
  u8 group = 0;
  u8 priority = 0;
  u8 attributes = 0;
  u8 routing = 0;
  ProgramParam program;
  SplitParam split;
  SampleParam sample;
};

struct SetbTimbreDefinition {
  u32 set = 0;
  u32 timbre = 0;
  u32 offset = 0;
  std::vector<SetbNoteParam> notes;
};

struct BankDefinition {
  std::vector<std::optional<SampleSetParam>> sampleSets;
  std::vector<ProgramDefinition> programs;
  std::vector<SetbTimbreDefinition> setbTimbres;
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

[[nodiscard]] SetbNoteParam readSetbNote(ByteReader reader, u32 offset, u8 size, u8 key) {
  const u16 vag = reader.le16(offset);
  const u8 velocityPitchCenter = reader.u8At(offset + 44);
  const u8 velocityAmpCenter = reader.u8At(offset + 45);
  return SetbNoteParam{
      .offset = offset,
      .size = size,
      .key = key,
      .velocityCurve = reader.u8At(offset + 2),
      .groupLimit = reader.u8At(offset + 7),
      .group = reader.u8At(offset + 8),
      .priority = reader.u8At(offset + 9),
      .attributes = reader.u8At(offset + 12),
      .routing = reader.u8At(offset + 13),
      .program =
          ProgramParam{
              .offset = offset,
              .volume = reader.u8At(offset + 3),
              .pan = static_cast<s8>(reader.u8At(offset + 4)),
              .transpose = static_cast<s8>(reader.u8At(offset + 5)),
              .detune = static_cast<s8>(reader.u8At(offset + 6)),
              .pitchWave = reader.u8At(offset + 18),
              .ampWave = reader.u8At(offset + 19),
              .pitchPhase = reader.u8At(offset + 20),
              .ampPhase = reader.u8At(offset + 21),
              .pitchRandomPhase = reader.u8At(offset + 22),
              .ampRandomPhase = reader.u8At(offset + 23),
              .pitchCycle = reader.le16(offset + 24),
              .ampCycle = reader.le16(offset + 26),
              .pitchPositive = static_cast<s16>(reader.le16(offset + 28)),
              .pitchNegative = static_cast<s16>(reader.le16(offset + 30)),
              .ampPositive = static_cast<s8>(reader.u8At(offset + 32)),
              .ampNegative = static_cast<s8>(reader.u8At(offset + 33)),
          },
      .split =
          SplitParam{
              .low = key,
              .cross = key,
              .high = key,
              .volume = 128,
              .pan = 64,
          },
      .sample =
          SampleParam{
              .offset = offset,
              .vag = vag,
              .velocityPitch = static_cast<s8>(reader.u8At(offset + 42)),
              .velocityPitchCenter = velocityPitchCenter,
              .velocityPitchCurve = reader.u8At(offset + 46),
              .velocityAmp = static_cast<s8>(reader.u8At(offset + 43)),
              .velocityAmpCenter = velocityAmpCenter,
              .velocityAmpCurve = reader.u8At(offset + 47),
              .baseNote = key,
              .adsr1 = reader.le16(offset + 14),
              .adsr2 = reader.le16(offset + 16),
              .pitchDelay = reader.le16(offset + 34),
              .pitchFade = reader.le16(offset + 38),
              .ampDelay = reader.le16(offset + 36),
              .ampFade = reader.le16(offset + 40),
              .lfoAttributes = reader.u8At(offset + 49),
              .routing = reader.u8At(offset + 13),
          },
  };
}

[[nodiscard]] std::vector<std::optional<SampleSetParam>> readSampleSets(ByteReader reader, const Chunk& sampleSets,
                                                                        const Chunk& samples) {
  std::vector<std::optional<SampleSetParam>> result(sampleSets.entries.size());
  for (u32 index = 0; index < sampleSets.entries.size(); ++index) {
    const auto offset = sampleSets.entries[index];
    if (!offset || !reader.has(*offset, 4)) {
      continue;
    }
    const u8 count = reader.u8At(*offset + 3);
    if (!reader.has(*offset + 4, static_cast<u64>(count) * 2)) {
      continue;
    }
    SampleSetParam set{
        .offset = *offset,
        .velocityCurve = reader.u8At(*offset),
        .velocityLow = std::clamp<int>(reader.u8At(*offset + 1) & 0x7f, 1, 127),
        .velocityHigh = std::clamp<int>(reader.u8At(*offset + 2) & 0x7f, 1, 127),
    };
    for (u32 number = 0; number < count; ++number) {
      const u16 sampleIndex = reader.le16(*offset + 4 + number * 2);
      if (sampleIndex < samples.entries.size() && samples.entries[sampleIndex] &&
          reader.has(*samples.entries[sampleIndex], kSampleBytes)) {
        set.samples.push_back(readSample(reader, *samples.entries[sampleIndex]));
      }
    }
    result[index] = std::move(set);
  }
  return result;
}

[[nodiscard]] std::vector<ProgramDefinition> readPrograms(ByteReader reader, const Chunk& programs) {
  std::vector<ProgramDefinition> result;
  for (u32 index = 0; index < programs.entries.size(); ++index) {
    const auto offset = programs.entries[index];
    if (!offset || !reader.has(*offset, kProgramBytes)) {
      continue;
    }
    const ProgramParam program = readProgram(reader, *offset);
    if (program.splitCount == 0 || reader.u8At(*offset + 5) != kSplitBytes ||
        !reader.has(program.splitOffset, static_cast<u64>(program.splitCount) * kSplitBytes)) {
      continue;
    }
    ProgramDefinition definition{.index = index, .program = program};
    definition.splits.reserve(program.splitCount);
    for (u32 split = 0; split < program.splitCount; ++split) {
      definition.splits.push_back(readSplit(reader, program.splitOffset + split * kSplitBytes));
    }
    result.push_back(std::move(definition));
  }
  return result;
}

[[nodiscard]] std::vector<SetbTimbreDefinition> readSetbTimbres(ByteReader reader, const Chunk& setb) {
  std::vector<SetbTimbreDefinition> result;
  for (u32 setIndex = 0; setIndex < setb.entries.size(); ++setIndex) {
    const auto setOffset = setb.entries[setIndex];
    if (!setOffset || !reader.has(*setOffset, 4)) {
      continue;
    }
    const u32 maximumTimbre = reader.le32(*setOffset);
    if (maximumTimbre >= 128 || !reader.has(*setOffset + 4, static_cast<u64>(maximumTimbre + 1) * 4)) {
      continue;
    }
    for (u32 timbre = 0; timbre <= maximumTimbre; ++timbre) {
      const u32 relative = reader.le32(*setOffset + 4 + timbre * 4);
      if (relative == 0xffffffff || relative >= setb.size) {
        continue;
      }
      const u32 timbreOffset = setb.offset + relative;
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
      SetbTimbreDefinition definition{
          .set = setIndex,
          .timbre = timbre,
          .offset = timbreOffset,
      };
      definition.notes.reserve(noteHigh - noteLow + 1);
      for (u32 note = noteLow; note <= noteHigh; ++note) {
        definition.notes.push_back(
            readSetbNote(reader, noteBlockOffset + (note - noteLow) * noteBytes, noteBytes, static_cast<u8>(note)));
      }
      result.push_back(std::move(definition));
    }
  }
  return result;
}

[[nodiscard]] BankDefinition readBankDefinition(ByteReader reader, const std::optional<Chunk>& programs,
                                                const std::optional<Chunk>& sampleSets,
                                                const std::optional<Chunk>& samples, const std::optional<Chunk>& setb) {
  BankDefinition result;
  if (programs && sampleSets && samples) {
    result.sampleSets = readSampleSets(reader, *sampleSets, *samples);
    result.programs = readPrograms(reader, *programs);
  }
  if (setb) {
    result.setbTimbres = readSetbTimbres(reader, *setb);
  }
  return result;
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
  switch (type & 0x0f) {
    case 0:
    case 6:
      return velocity;
    case 1:
    case 7:
      return 128 - velocity;
    case 2:
    case 8:
      return convexVelocity(velocity);
    case 3:
    case 9:
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

[[nodiscard]] int wrappedFixedSquare(int value) {
  // The driver reads the low 32 bits of the MIPS multiply before dividing the
  // signed 18.14 result. Preserve that wrap, including its type 4/5 curve.
  const auto product = static_cast<u32>(static_cast<s64>(value) * value);
  return std::bit_cast<s32>(product) / 0x4000;
}

[[nodiscard]] int endpointDenominator(int velocity, int center) {
  return velocity > center ? 127 - center : center - 1;
}

[[nodiscard]] double bipolarVelocityCurve(u8 type, int velocity, int center) {
  velocity = std::clamp(velocity, 1, 127);
  center = std::clamp(center, 0, 127);
  const int difference = velocity - center;
  const auto linear = [&] { return difference * 0x10000 / 126; };
  const auto convex = [](int value) { return wrappedFixedSquare((value - 1) * 0x8000 / 126); };
  const auto concave = [&](int value) { return 0x10000 - wrappedFixedSquare(0x10000 - (value - 1) * 0x8000 / 126); };
  const auto endpoint = [&] {
    const int denominator = endpointDenominator(velocity, center);
    return difference == 0 || denominator == 0 ? 0 : difference * 0x10000 / denominator;
  };
  const auto endpointConvex = [&] {
    const int denominator = endpointDenominator(velocity, center);
    if (difference == 0 || denominator == 0) {
      return 0;
    }
    const int amount = wrappedFixedSquare(difference * 0x8000 / denominator);
    return difference < 0 ? -amount : amount;
  };
  const auto endpointConcave = [&] {
    const int denominator = endpointDenominator(velocity, center);
    if (difference == 0 || denominator == 0) {
      return 0;
    }
    const int scaled = difference * 0x8000 / denominator;
    return difference > 0 ? 0x10000 - wrappedFixedSquare(0x8000 - scaled)
                          : wrappedFixedSquare(0x8000 + scaled) - 0x10000;
  };
  int fixed = 0;
  switch (type & 0x0f) {
    case 0:
      fixed = linear();
      break;
    case 1:
      fixed = -linear();
      break;
    case 2:
      fixed = convex(velocity) - convex(center);
      break;
    case 3:
      fixed = convex(center) - convex(velocity);
      break;
    case 4:
      fixed = concave(velocity) - concave(center);
      break;
    case 5:
      fixed = concave(center) - concave(velocity);
      break;
    case 6:
      fixed = endpoint();
      break;
    case 7:
      fixed = -endpoint();
      break;
    case 8:
      fixed = endpointConvex();
      break;
    case 9:
      fixed = endpointConcave();
      break;
    case 10:
      // The shipped dispatch table points this undocumented value at the
      // ordinary byte-valued velocity helper.
      fixed = velocity;
      break;
    default:
      fixed = linear();
      break;
  }
  return fixed / 65536.0;
}

[[nodiscard]] int panMagnitude(s8 value) {
  return std::min(std::abs(static_cast<int>(value)), 127);
}

[[nodiscard]] int regionPan(const ProgramParam& program, const SplitParam& split, const SampleParam& sample, int key) {
  const int follow = ((key - static_cast<int>(program.keyFollowPanCenter)) * program.keyFollowPan +
                      (key - static_cast<int>(split.keyFollowPanCenter)) * split.keyFollowPan) /
                     12;
  if ((program.attributes & 1) == 0) {
    return std::clamp(panMagnitude(program.pan) + panMagnitude(split.pan) + panMagnitude(sample.pan) - 128 + follow, 0,
                      127);
  }
  int pan = program.pan + split.pan + sample.pan - 128 + follow;
  while (pan > 127) {
    pan -= 255;
  }
  while (pan < -128) {
    pan += 255;
  }
  return std::min(std::abs(pan), 127);
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

[[nodiscard]] double attenuation(double gain) {
  return gain <= 0.0 ? 96.0 : -20.0 * std::log10(gain);
}

[[nodiscard]] double reverbSend(const SoundBankData& bank) {
  return bank.reverbType == 0 ? 0.0 : std::min(bank.reverbDepth, 0x3fffu) / static_cast<double>(0x3fff);
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

[[nodiscard]] bool unsupportedLfoWaveform(u8 value) {
  return value != 0 && !waveform(value).has_value();
}

[[nodiscard]] bool pitchLfoStartsAtKeyOn(u8 attributes) {
  return (attributes & 0x05) != 0;
}

[[nodiscard]] bool ampLfoStartsAtKeyOn(u8 attributes) {
  return (attributes & 0x50) != 0;
}

[[nodiscard]] bool hasKeyOffLfoTrigger(u8 attributes) {
  return (attributes & 0x66) != 0;
}

[[nodiscard]] bool unsupportedLfoDepthPair(int positive, int negative) {
  return positive != negative || positive < 0;
}

[[nodiscard]] InstrumentModulation modulation(const ProgramParam& program, const SplitParam& split,
                                              const SampleParam& sample, int key, int velocity) {
  InstrumentModulation result;
  const int pitchKey = (key - static_cast<int>(split.keyFollowPitchCenter)) * split.keyFollowPitch / 12;
  const int pitchVelocity = static_cast<int>(
      bipolarVelocityCurve(sample.velocityPitchCurve, velocity, sample.velocityPitchCenter) * sample.velocityPitch);
  const int fixedPitchUnits =
      std::max(std::abs(std::clamp(program.pitchPositive + pitchKey + pitchVelocity, -32768, 32767)),
               std::abs(std::clamp(program.pitchNegative + pitchKey + pitchVelocity, -32768, 32767)));
  const int controlledPitchUnits = std::max(std::abs(static_cast<int>(program.midiPitchPositive)),
                                            std::abs(static_cast<int>(program.midiPitchNegative)));
  const int pitchUnits = fixedPitchUnits != 0 ? fixedPitchUnits : controlledPitchUnits;
  const auto pitchWaveform = waveform(program.pitchWave);
  if (pitchLfoStartsAtKeyOn(sample.lfoAttributes) && pitchWaveform && program.pitchCycle != 0 && pitchUnits != 0) {
    result.vibrato = VibratoSpec{
        .maxDepthCents = pitchUnits * 100.0 / 128.0,
        .rateHertz = ModulationRange{1000.0 / program.pitchCycle, 1000.0 / program.pitchCycle},
        .waveform = pitchWaveform,
        .delaySeconds = ModulationRange{sample.pitchDelay / 1000.0, sample.pitchDelay / 1000.0},
        .depthMode = fixedPitchUnits != 0 ? ModulationDepthMode::Fixed : ModulationDepthMode::Controller,
    };
  }
  const int ampKey = (key - static_cast<int>(split.keyFollowAmpCenter)) * split.keyFollowAmp / 12;
  const int ampVelocity = static_cast<int>(
      bipolarVelocityCurve(sample.velocityAmpCurve, velocity, sample.velocityAmpCenter) * sample.velocityAmp);
  const int fixedAmpUnits = std::max(std::abs(std::clamp(program.ampPositive + ampKey + ampVelocity, -128, 127)),
                                     std::abs(std::clamp(program.ampNegative + ampKey + ampVelocity, -128, 127)));
  const int controlledAmpUnits = std::max(std::abs(static_cast<int>(program.midiAmpPositive)),
                                          std::abs(static_cast<int>(program.midiAmpNegative)));
  const int ampUnits = fixedAmpUnits != 0 ? fixedAmpUnits : controlledAmpUnits;
  const double ampDepth = std::clamp(ampUnits / 128.0, 0.0, 0.999);
  const auto ampWaveform = waveform(program.ampWave);
  if (ampLfoStartsAtKeyOn(sample.lfoAttributes) && ampWaveform && program.ampCycle != 0 && ampDepth != 0.0) {
    result.tremolo = TremoloSpec{
        .maxDepthDb = -20.0 * std::log10(1.0 - ampDepth),
        .rateHertz = ModulationRange{1000.0 / program.ampCycle, 1000.0 / program.ampCycle},
        .waveform = ampWaveform,
        .gainMode = TremoloGainMode::BipolarAroundNominal,
        .delaySeconds = ModulationRange{sample.ampDelay / 1000.0, sample.ampDelay / 1000.0},
        .depthMode = fixedAmpUnits != 0 ? ModulationDepthMode::Fixed : ModulationDepthMode::Controller,
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

struct RegionShape {
  u16 keys = 1;
  u16 velocities = 1;
};

struct RegionResolution {
  u32 step = 1;
  u64 exactRegions = 0;
  u64 emittedRegions = 0;
};

struct SynthWarnings {
  bool lfoShape = false;
  bool lfoPhase = false;
  bool lfoKeyOff = false;
  bool lfoAsymmetry = false;
  bool lfoCombination = false;
  bool customVelocityCurve = false;
  bool bendRange = false;
  bool panPhase = false;
  bool noise = false;
  bool voicePolicy = false;
  bool routing = false;
  bool setbVoicePolicy = false;
};

void warnCustomVelocityCurve(InstrumentSetBuilder& instruments, u8 curves, SourceRange range, SynthWarnings& warnings) {
  if ((curves & 0xf0) == 0 || warnings.customVelocityCurve) {
    return;
  }
  instruments.warning("SonyPS2 application-supplied velocity remap tables are unavailable; using the selected "
                      "built-in curve",
                      range);
  warnings.customVelocityCurve = true;
}

void warnRouting(InstrumentSetBuilder& instruments, u8 routing, SourceRange range, SynthWarnings& warnings) {
  if (routing == 0 || warnings.routing) {
    return;
  }
  instruments.warning("SonyPS2 reverb algorithm, SPU2 core, and dry/wet routing are reduced to a generic instrument "
                      "reverb send",
                      range);
  warnings.routing = true;
}

void warnLfoLimitations(InstrumentSetBuilder& instruments, const ProgramParam& program, const SampleParam& sample,
                        SourceRange programRange, SourceRange sampleRange, bool checkAsymmetry,
                        SynthWarnings& warnings) {
  if ((unsupportedLfoWaveform(program.pitchWave) || unsupportedLfoWaveform(program.ampWave)) && !warnings.lfoShape) {
    instruments.warning("SonyPS2 custom or unknown LFO waveform is unavailable; the LFO is disabled", programRange);
    warnings.lfoShape = true;
  }
  if ((program.pitchPhase != 0 || program.ampPhase != 0 || program.pitchRandomPhase != 0 ||
       program.ampRandomPhase != 0 || sample.pitchFade != 0 || sample.ampFade != 0) &&
      !warnings.lfoPhase) {
    instruments.warning("SonyPS2 LFO phase, random phase, or fade behavior is not representable", sampleRange);
    warnings.lfoPhase = true;
  }
  if (hasKeyOffLfoTrigger(sample.lfoAttributes) && !warnings.lfoKeyOff) {
    instruments.warning("SonyPS2 key-off LFO triggering has no synth-model equivalent", sampleRange);
    warnings.lfoKeyOff = true;
  }
  if (checkAsymmetry && !warnings.lfoAsymmetry &&
      (unsupportedLfoDepthPair(program.pitchPositive, program.pitchNegative) ||
       unsupportedLfoDepthPair(program.ampPositive, program.ampNegative))) {
    instruments.warning("SonyPS2 signed or asymmetric LFO half-cycle depths are reduced to a symmetric maximum",
                        sampleRange);
    warnings.lfoAsymmetry = true;
  }
}

[[nodiscard]] std::optional<RegionShape> programRegionShape(const ProgramParam& program, const SplitParam& split,
                                                            const SampleParam& sample, const SampleSetParam& sampleSet,
                                                            const SoundBankData& layout) {
  if (sample.vag == 0xffff || sample.vag >= layout.vags.size() || !layout.vags[sample.vag]) {
    return std::nullopt;
  }
  const int keyLow = split.low & 0x7f;
  const int keyHigh = split.high & 0x7f;
  const int velocityLow = std::max(sampleSet.velocityLow, static_cast<int>(sample.low & 0x7f));
  const int velocityHigh = std::min(sampleSet.velocityHigh, static_cast<int>(sample.high & 0x7f));
  if (keyLow > keyHigh || velocityLow > velocityHigh) {
    return std::nullopt;
  }
  return RegionShape{
      .keys = static_cast<u16>(keyDependent(program, split, sample) ? keyHigh - keyLow + 1 : 1),
      .velocities = static_cast<u16>(velocityDependent(sample) || sampleSet.velocityCurve != 0
                                         ? midiVelocity(static_cast<u8>(velocityHigh)) -
                                               midiVelocity(static_cast<u8>(velocityLow)) + 1
                                         : 1),
  };
}

void appendProgramRegionShapes(std::vector<RegionShape>& shapes, const BankDefinition& definition,
                               const SoundBankData& layout) {
  for (const auto& program : definition.programs) {
    for (const auto& split : program.splits) {
      if (split.sampleSet >= definition.sampleSets.size() || !definition.sampleSets[split.sampleSet]) {
        continue;
      }
      const auto& sampleSet = *definition.sampleSets[split.sampleSet];
      for (const auto& sample : sampleSet.samples) {
        if (auto shape = programRegionShape(program.program, split, sample, sampleSet, layout)) {
          shapes.push_back(*shape);
        }
      }
    }
  }
}

void appendSetbRegionShapes(std::vector<RegionShape>& shapes, const BankDefinition& definition,
                            const SoundBankData& layout) {
  for (const auto& timbre : definition.setbTimbres) {
    for (const auto& note : timbre.notes) {
      if (note.sample.vag != 0xffff && note.sample.vag < layout.vags.size() && layout.vags[note.sample.vag]) {
        const bool velocityZones =
            note.velocityCurve != 0 || note.sample.velocityPitch != 0 || note.sample.velocityAmp != 0;
        shapes.push_back(RegionShape{
            .velocities = static_cast<u16>(velocityZones ? 128 - midiVelocity(1) : 1),
        });
      }
    }
  }
}

[[nodiscard]] RegionResolution regionResolution(const BankDefinition& definition, const SoundBankData& layout) {
  std::vector<RegionShape> shapes;
  appendProgramRegionShapes(shapes, definition, layout);
  appendSetbRegionShapes(shapes, definition, layout);

  const auto countAt = [&](u32 step) {
    u64 count = 0;
    for (const auto& shape : shapes) {
      count += ((shape.keys + step - 1) / step) * ((shape.velocities + step - 1) / step);
    }
    return count;
  };
  RegionResolution resolution{.exactRegions = countAt(1)};
  while (resolution.step < 128 && countAt(resolution.step) > kMaxSynthRegions) {
    ++resolution.step;
  }
  resolution.emittedRegions = countAt(resolution.step);
  return resolution;
}

void emitProgramSampleRegions(const InstrumentSetBuilder::Entry& instrument, ByteReader reader,
                              const ProgramParam& program, const SplitParam& split, const SampleSetParam& sampleSet,
                              const SampleParam& sample, const VagInfo& vag, RegionResolution resolution) {
  const int keyLow = std::min<int>(split.low & 0x7f, 127);
  const int keyHigh = std::min<int>(split.high & 0x7f, 127);
  const int rawVelocityLow = std::max<int>(sampleSet.velocityLow, sample.low & 0x7f);
  const int rawVelocityHigh = std::min<int>(sampleSet.velocityHigh, sample.high & 0x7f);
  if (rawVelocityLow > rawVelocityHigh) {
    return;
  }
  const int targetVelocityLow = midiVelocity(static_cast<u8>(rawVelocityLow));
  const int targetVelocityHigh = midiVelocity(static_cast<u8>(rawVelocityHigh));
  const bool splitKeys = keyDependent(program, split, sample);
  const bool splitVelocities = velocityDependent(sample) || sampleSet.velocityCurve != 0;
  for (int key = keyLow; key <= keyHigh;) {
    const int keyHighOut = splitKeys ? std::min(key + static_cast<int>(resolution.step) - 1, keyHigh) : keyHigh;
    const int representedKey =
        splitKeys ? key + (keyHighOut - key) / 2 : std::clamp<int>(split.keyFollowPitchCenter, keyLow, keyHigh);
    for (int velocity = targetVelocityLow; velocity <= targetVelocityHigh;) {
      const int velocityHighOut = splitVelocities
                                      ? std::min(velocity + static_cast<int>(resolution.step) - 1, targetVelocityHigh)
                                      : targetVelocityHigh;
      const int representedVelocity = splitVelocities
                                          ? velocity + (velocityHighOut - velocity) / 2
                                          : midiVelocity(static_cast<u8>(std::clamp<int>(
                                                sample.velocityAmpCenter, rawVelocityLow, rawVelocityHigh)));
      const int rawVelocity = splitVelocities
                                  ? std::clamp<int>(rawVelocityFromMidi(static_cast<u8>(representedVelocity)),
                                                    rawVelocityLow, rawVelocityHigh)
                                  : std::clamp<int>(sample.velocityAmpCenter, rawVelocityLow, rawVelocityHigh);
      // Region ranges use MIDI velocity, while driver curves are evaluated in
      // Sony's linear source domain.
      const double gain =
          gainFromRaw(program.volume) * gainFromRaw(split.volume) * gainFromRaw(sample.volume) *
          crossfade(split.low, split.cross, split.high, representedKey) *
          crossfade(sample.low, sample.cross, sample.high, rawVelocity) *
          velocityCurveCorrection(sampleSet.velocityCurve, rawVelocity, static_cast<u8>(representedVelocity));
      Region region{
          .keyRange = KeyRange{static_cast<u8>(key), static_cast<u8>(keyHighOut)},
          .velocityRange = VelocityRange{static_cast<u8>(velocity), static_cast<u8>(velocityHighOut)},
          .range = reader.range(sample.offset, kSampleBytes),
          .unityKey = sample.baseNote - program.transpose - split.transpose -
                      (program.detune + split.detune + sample.detune) / 128.0 +
                      12.0 * std::log2(48000.0 / vag.sampleRate),
          .envelope = keyFollowEnvelope(sample, representedKey),
          .pan = panPositionFrom7Bit(static_cast<u8>(regionPan(program, split, sample, representedKey))),
          .attenuationDb = attenuation(gain),
          .modulation = modulation(program, split, sample, representedKey, rawVelocity),
      };
      instrument.region(SampleRef::unbound(sample.vag), std::move(region))
          .source("Sample region", reader.range(sample.offset, kSampleBytes), "sony-ps2-sample-param");
      velocity = velocityHighOut + 1;
    }
    key = keyHighOut + 1;
  }
}

void addProgramSample(InstrumentSetBuilder& instruments, const InstrumentSetBuilder::Entry& instrument,
                      ByteReader reader, const SoundBankData& layout, const ProgramParam& program,
                      const SplitParam& split, const SampleSetParam& sampleSet, const SampleParam& sample,
                      RegionResolution resolution, SynthWarnings& warnings) {
  const SourceRange sampleRange = reader.range(sample.offset, kSampleBytes);
  warnCustomVelocityCurve(instruments, sample.velocityPitchCurve | sample.velocityAmpCurve, sampleRange, warnings);
  const bool mixedPitchDepth = pitchLfoStartsAtKeyOn(sample.lfoAttributes) && program.pitchWave != 0 &&
                               program.pitchCycle != 0 &&
                               (program.midiPitchPositive != 0 || program.midiPitchNegative != 0) &&
                               (program.pitchPositive != 0 || program.pitchNegative != 0 || split.keyFollowPitch != 0 ||
                                sample.velocityPitch != 0);
  const bool mixedAmpDepth =
      ampLfoStartsAtKeyOn(sample.lfoAttributes) && program.ampWave != 0 && program.ampCycle != 0 &&
      (program.midiAmpPositive != 0 || program.midiAmpNegative != 0) &&
      (program.ampPositive != 0 || program.ampNegative != 0 || split.keyFollowAmp != 0 || sample.velocityAmp != 0);
  if (!warnings.lfoCombination && (mixedPitchDepth || mixedAmpDepth)) {
    instruments.warning("SonyPS2 additive fixed and controller LFO depths share one synth modulation mode; the "
                        "fixed component is retained",
                        reader.range(sample.offset, kSampleBytes));
    warnings.lfoCombination = true;
  }
  if ((sample.group != 0 || sample.priority != 0) && !warnings.voicePolicy) {
    // These fields control admission and stealing of live SPU2 voices; they do
    // not alter a region's static synthesis parameters.
    instruments.warning("SonyPS2 voice groups and priorities have no synth-model equivalent",
                        reader.range(sample.offset, kSampleBytes));
    warnings.voicePolicy = true;
  }
  // Preserve effect-send capability as Instrument::reverb. The exact dry/wet
  // matrix and SPU2 core selection need a routing model.
  warnRouting(instruments, sample.routing, sampleRange, warnings);
  if (sample.pan < 0 && !warnings.panPhase) {
    instruments.warning("SonyPS2 negative-phase pan is reduced to ordinary stereo position",
                        reader.range(sample.offset, kSampleBytes));
    warnings.panPhase = true;
  }
  if (sample.vag == 0xffff) {
    if (!warnings.noise) {
      instruments.warning("SonyPS2 noise-generator regions cannot be represented as sampled regions",
                          reader.range(sample.offset, kSampleBytes));
      warnings.noise = true;
    }
    return;
  }
  if (sample.vag >= layout.vags.size() || !layout.vags[sample.vag]) {
    return;
  }

  emitProgramSampleRegions(instrument, reader, program, split, sampleSet, sample, *layout.vags[sample.vag], resolution);

  // Physical depth/rate/delay are retained; unsupported phase and trigger
  // behavior remains an explicit diagnostic.
  warnLfoLimitations(instruments, program, sample, reader.range(program.offset, kProgramBytes), sampleRange, false,
                     warnings);
}

void addProgramDefinition(InstrumentSetBuilder& instruments, ByteReader reader, SoundBankData& layout,
                          const BankDefinition& definition, const ProgramDefinition& source,
                          RegionResolution resolution, SynthWarnings& warnings) {
  const ProgramParam& program = source.program;
  ProgramRuntimeInfo runtime{
      .program = static_cast<u8>(source.index),
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
  for (const auto& split : source.splits) {
    runtime.pitchBendNegative = std::max(runtime.pitchBendNegative, split.bendLow);
    runtime.pitchBendPositive = std::max(runtime.pitchBendPositive, split.bendHigh);
    runtime.pitchBendZones.push_back(PitchBendZone{
        .keyLow = static_cast<u8>(split.low & 0x7f),
        .keyHigh = static_cast<u8>(split.high & 0x7f),
        .negative = split.bendLow,
        .positive = split.bendHigh,
    });
  }
  if (!warnings.bendRange &&
      (std::ranges::any_of(source.splits, [](const SplitParam& split) { return split.bendLow != split.bendHigh; }) ||
       std::ranges::any_of(source.splits, [&](const SplitParam& split) {
         return split.bendLow != source.splits.front().bendLow || split.bendHigh != source.splits.front().bendHigh;
       }))) {
    // Sequence playback retains the driver's physical, direction-specific
    // result for each active split. The static Instrument range is only the
    // symmetric channel capacity used for MIDI and live input; simultaneous
    // voices with different ranges cannot be expressed on one MIDI channel.
    instruments.warning("SonyPS2 split- or direction-specific pitch bends use one live-input channel range",
                        reader.range(program.offset, kProgramBytes));
    warnings.bendRange = true;
  }
  if (!warnings.lfoAsymmetry && (unsupportedLfoDepthPair(program.pitchPositive, program.pitchNegative) ||
                                 unsupportedLfoDepthPair(program.midiPitchPositive, program.midiPitchNegative) ||
                                 unsupportedLfoDepthPair(program.ampPositive, program.ampNegative) ||
                                 unsupportedLfoDepthPair(program.midiAmpPositive, program.midiAmpNegative))) {
    // The driver selects independently signed depths for each waveform half.
    // The synth model can retain only one symmetric magnitude.
    instruments.warning("SonyPS2 signed or asymmetric LFO half-cycle depths are reduced to a symmetric maximum",
                        reader.range(program.offset, kProgramBytes));
    warnings.lfoAsymmetry = true;
  }

  const bool wet = std::ranges::any_of(source.splits, [&](const SplitParam& split) {
    return split.sampleSet < definition.sampleSets.size() && definition.sampleSets[split.sampleSet] &&
           std::ranges::any_of(
               definition.sampleSets[split.sampleSet]->samples,
               [](const SampleParam& sample) { return (sample.routing & 0x0c) != 0; });
  });
  layout.runtimePrograms.push_back(runtime);
  auto instrument = instruments.append(Instrument{
      .explicitAddress = InstrumentAddress{.bank = 0, .program = source.index},
      .identity = instrumentIdentity(0, static_cast<u8>(source.index)),
      .pitchBendRangeCents = static_cast<u16>(
          std::min<u32>(65535, (std::max(runtime.pitchBendPositive, runtime.pitchBendNegative) * 100u + 127u) / 128u)),
      .reverb = wet ? reverbSend(layout) : 0.0,
      .name = fmt::format("Program {}", source.index),
      .range = reader.range(program.offset, kProgramBytes),
  });
  instrument.source(instrument.value().name, reader.range(program.offset, kProgramBytes), "sony-ps2-program");

  for (const auto& split : source.splits) {
    if (split.sampleSet >= definition.sampleSets.size() || !definition.sampleSets[split.sampleSet]) {
      continue;
    }
    const auto& sampleSet = *definition.sampleSets[split.sampleSet];
    warnCustomVelocityCurve(instruments, sampleSet.velocityCurve, reader.range(sampleSet.offset, 1), warnings);
    for (const auto& sample : sampleSet.samples) {
      addProgramSample(instruments, instrument, reader, layout, program, split, sampleSet, sample, resolution,
                       warnings);
    }
  }

  if (((program.attributes & 1) != 0 || program.pan < 0 ||
       std::ranges::any_of(
           source.splits, [](const SplitParam& split) { return split.pan < 0; })) &&
      !warnings.panPhase) {
    // ROUND_PAN and the driver's negative pan values can invert SPU output
    // phase. Region::pan can preserve position but not that phase inversion.
    instruments.warning("SonyPS2 round-pan phase inversion is reduced to ordinary stereo position",
                        reader.range(program.offset, kProgramBytes));
    warnings.panPhase = true;
  }
}

void addSetbNote(InstrumentSetBuilder& instruments, const InstrumentSetBuilder::Entry& instrument, ByteReader reader,
                 const SoundBankData& layout, const SetbNoteParam& note, RegionResolution resolution,
                 SynthWarnings& warnings) {
  const u16 vagIndex = note.sample.vag;
  if (vagIndex == 0xffff) {
    if (!warnings.noise) {
      instruments.warning("SonyPS2 noise-generator regions cannot be represented as sampled regions",
                          reader.range(note.offset, note.size));
      warnings.noise = true;
    }
    return;
  }
  if (vagIndex >= layout.vags.size() || !layout.vags[vagIndex]) {
    return;
  }
  const SourceRange noteRange = reader.range(note.offset, note.size);
  warnRouting(instruments, note.routing, noteRange, warnings);
  const ProgramParam& lfo = note.program;
  const SampleParam& lfoSample = note.sample;
  warnCustomVelocityCurve(instruments, note.velocityCurve | lfoSample.velocityPitchCurve | lfoSample.velocityAmpCurve,
                          noteRange, warnings);
  warnLfoLimitations(instruments, lfo, lfoSample, noteRange, noteRange, true, warnings);

  const VagInfo& vag = *layout.vags[vagIndex];
  const bool velocityZones = note.velocityCurve != 0 || lfoSample.velocityPitch != 0 || lfoSample.velocityAmp != 0;
  for (int velocity = midiVelocity(1); velocity <= 127;) {
    const int high = velocityZones ? std::min(velocity + static_cast<int>(resolution.step) - 1, 127) : 127;
    const int representedVelocity = velocityZones ? velocity + (high - velocity) / 2 : 127;
    const int rawVelocity = velocityZones ? rawVelocityFromMidi(static_cast<u8>(representedVelocity)) : 127;
    const double gain = gainFromRaw(lfo.volume) *
                        velocityCurveCorrection(note.velocityCurve, rawVelocity, static_cast<u8>(representedVelocity));
    Region region{
        .keyRange = KeyRange{note.key, note.key},
        .velocityRange = VelocityRange{static_cast<u8>(velocity), static_cast<u8>(high)},
        .range = reader.range(note.offset, note.size),
        .unityKey = note.key - lfo.transpose - lfo.detune / 128.0 + 12.0 * std::log2(48000.0 / vag.sampleRate),
        .envelope = psxSpuEnvelope(lfoSample.adsr1, lfoSample.adsr2, PsxSpuGeneration::Ps2),
        .pan = panPositionFrom7Bit(static_cast<u8>(panMagnitude(lfo.pan))),
        .attenuationDb = attenuation(gain),
        .modulation = modulation(lfo, note.split, lfoSample, note.key, rawVelocity),
    };
    instrument.region(SampleRef::unbound(vagIndex), std::move(region))
        .source("SE timbre note", reader.range(note.offset, note.size), "sony-ps2-setb-note");
    velocity = high + 1;
  }
  if ((note.groupLimit != 0 || note.group != 0 || note.priority != 0) && !warnings.setbVoicePolicy) {
    // Group limits and priorities affect voice stealing rather than the static
    // sound of an exported region.
    instruments.warning("SonyPS2 Setb group limits and voice priorities have no synth-model equivalent",
                        reader.range(note.offset, note.size));
    warnings.setbVoicePolicy = true;
  }
  if (((note.attributes & 1) != 0 || lfo.pan < 0) && !warnings.panPhase) {
    instruments.warning("SonyPS2 round- or negative-phase pan is reduced to ordinary stereo position",
                        reader.range(note.offset, note.size));
    warnings.panPhase = true;
  }
}

void addSetbDefinitions(InstrumentSetBuilder& instruments, ByteReader reader, const SoundBankData& layout,
                        const BankDefinition& definition, RegionResolution resolution, SynthWarnings& warnings) {
  for (const auto& timbre : definition.setbTimbres) {
    const bool wet =
        std::ranges::any_of(timbre.notes, [](const SetbNoteParam& note) { return (note.routing & 0x0c) != 0; });
    auto instrument = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 128 + timbre.set, .program = timbre.timbre},
        .identity = setbInstrumentIdentity(static_cast<u8>(timbre.set), static_cast<u8>(timbre.timbre)),
        .reverb = wet ? reverbSend(layout) : 0.0,
        .name = fmt::format("SE Set {} Timbre {}", timbre.set, timbre.timbre),
        .range = reader.range(timbre.offset, 8),
    });
    instrument.source(instrument.value().name, reader.range(timbre.offset, 8), "sony-ps2-setb-timbre");
    for (const auto& note : timbre.notes) {
      addSetbNote(instruments, instrument, reader, layout, note, resolution, warnings);
    }
  }
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
  const bool hasProgramTables = programs && sampleSets && samples;
  if (!hasProgramTables) {
    result.warning("SonyPS2 HD is missing its Prog, Sset, or Smpl chunk", reader.range(offset, headerBytes));
    if (!setb) {
      return;
    }
  }
  const BankDefinition definition = readBankDefinition(reader, programs, sampleSets, samples, setb);

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

  const RegionResolution resolution = regionResolution(definition, layout);
  if (resolution.step != 1) {
    const std::string consequence =
        resolution.emittedRegions <= kMaxSynthRegions
            ? "to fit 16-bit synth tables"
            : "but the coarsest zones still exceed the conservative 16-bit synth-table budget";
    instruments.warning(fmt::format("SonyPS2 key/velocity modulation requires {} exact regions; using {}-step "
                                    "zones ({} regions), {}",
                                    resolution.exactRegions, resolution.step, resolution.emittedRegions, consequence),
                        reader.range(offset, headerBytes));
  }

  SynthWarnings warnings;
  if (hasProgramTables) {
    for (const auto& program : definition.programs) {
      addProgramDefinition(instruments, reader, layout, definition, program, resolution, warnings);
    }
  }
  if (setb) {
    instruments.source(SourceRole::Table, "SonyPS2 Setb chunk", reader.range(setb->offset, setb->size),
                       "sony-ps2-setb");
    addSetbDefinitions(instruments, reader, layout, definition, resolution, warnings);
  }
  bank.data(std::move(layout));
}

}  // namespace vgmtrans::formats::sony_ps2
