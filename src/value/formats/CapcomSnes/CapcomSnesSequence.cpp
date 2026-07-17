/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnes.h"

#include "value/base/LevelScale.h"
#include "value/base/RecordReader.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/SequenceVm.h"

#include <fmt/format.h>

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

constexpr u8 kNoteOctaveMask = 0x07;
constexpr u8 kNoteOctaveUpMask = 0x08;
constexpr u8 kNoteDottedMask = 0x10;
constexpr u8 kNoteTripletMask = 0x20;
constexpr u8 kNoteSlurredMask = 0x40;

[[nodiscard]] constexpr SemanticCommandKind commandKind(CapcomSnesCommandKind kind) {
  return SemanticCommandKind{static_cast<u32>(kind)};
}

[[nodiscard]] constexpr SemanticOperandId operandId(CapcomSnesOperand operand) {
  return SemanticOperandId{static_cast<u32>(operand)};
}

[[nodiscard]] constexpr u32 profileFor(CapcomSnesEngineVersion version) {
  return static_cast<u32>(version);
}

[[nodiscard]] CapcomSnesEngineVersion versionFrom(const SequenceProgram& program) {
  switch (static_cast<CapcomSnesEngineVersion>(program.config.profile)) {
    case CapcomSnesEngineVersion::v1BgmInList:
    case CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation:
    case CapcomSnesEngineVersion::v3BgmFixedLocation:
      return static_cast<CapcomSnesEngineVersion>(program.config.profile);
    case CapcomSnesEngineVersion::none:
      // Directly constructed programs historically implied the newest driver.
      return CapcomSnesEngineVersion::v3BgmFixedLocation;
  }
  return CapcomSnesEngineVersion::v3BgmFixedLocation;
}

struct CommandSpec {
  CapcomSnesCommandKind kind = CapcomSnesCommandKind::Unsupported;
  std::string_view name = "Unsupported";
  std::string_view localKind = "unsupported";
  SequenceSemantic semantic = SequenceSemantic::Unsupported;
  CommandPlaybackStatus playback = CommandPlaybackStatus::Unsupported;
};

[[nodiscard]] constexpr CommandSpec spec(CapcomSnesCommandKind kind, std::string_view name,
                                         SequenceSemantic semantic = SequenceSemantic::State,
                                         CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback,
                                         std::string_view localKind = {}) {
  return CommandSpec{
      .kind = kind,
      .name = name,
      .localKind = localKind.empty() ? name : localKind,
      .semantic = semantic,
      .playback = playback,
  };
}

// The base opcode profile is plain data. Version differences are sparse patches
// below; operand reads and playback semantics stay in their respective switches.
constexpr auto kBaseOpcodeProfile = [] {
  std::array<CommandSpec, 0x20> table{};
  table[0x00] = spec(CapcomSnesCommandKind::ToggleTriplet, "Toggle Triplet");
  table[0x01] = spec(CapcomSnesCommandKind::ToggleSlur, "Toggle Slur");
  table[0x02] = spec(CapcomSnesCommandKind::DottedNote, "Dotted Note");
  table[0x03] = spec(CapcomSnesCommandKind::ToggleOctaveUp, "Toggle Octave Up");
  table[0x04] = spec(CapcomSnesCommandKind::NoteAttributes, "Note Attributes");
  table[0x05] = spec(CapcomSnesCommandKind::Tempo, "Tempo", SequenceSemantic::Tempo);
  table[0x06] = spec(CapcomSnesCommandKind::DurationRate, "Duration Rate");
  table[0x07] = spec(CapcomSnesCommandKind::Volume, "Volume", SequenceSemantic::Level);
  table[0x08] = spec(CapcomSnesCommandKind::Instrument, "Instrument", SequenceSemantic::Instrument);
  table[0x09] = spec(CapcomSnesCommandKind::Octave, "Octave");
  table[0x0a] = spec(CapcomSnesCommandKind::GlobalTranspose, "Global Transpose", SequenceSemantic::Pitch);
  table[0x0b] = spec(CapcomSnesCommandKind::Transpose, "Transpose", SequenceSemantic::Pitch);
  table[0x0c] = spec(CapcomSnesCommandKind::Tuning, "Tuning", SequenceSemantic::Pitch);
  table[0x0d] = spec(CapcomSnesCommandKind::PortamentoTime, "Portamento Time", SequenceSemantic::Portamento);
  for (u8 opcode = 0x0e; opcode <= 0x11; ++opcode) {
    table[opcode] = spec(CapcomSnesCommandKind::RepeatUntil, "Repeat Until", SequenceSemantic::Repeat,
                         CommandPlaybackStatus::AffectsControlFlow);
  }
  for (u8 opcode = 0x12; opcode <= 0x15; ++opcode) {
    table[opcode] = spec(CapcomSnesCommandKind::RepeatBreak, "Repeat Break", SequenceSemantic::RepeatBreak,
                         CommandPlaybackStatus::AffectsControlFlow);
  }
  table[0x16] =
      spec(CapcomSnesCommandKind::Jump, "Jump", SequenceSemantic::Jump, CommandPlaybackStatus::AffectsControlFlow);
  table[0x17] = spec(CapcomSnesCommandKind::End, "End", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback);
  table[0x18] = spec(CapcomSnesCommandKind::Pan, "Pan", SequenceSemantic::Pan);
  table[0x19] = spec(CapcomSnesCommandKind::MasterVolume, "Master Volume", SequenceSemantic::Level);
  table[0x1a] = spec(CapcomSnesCommandKind::Lfo, "LFO", SequenceSemantic::Modulation);
  table[0x1b] =
      spec(CapcomSnesCommandKind::EchoParam, "Echo Param", SequenceSemantic::Meta, CommandPlaybackStatus::SourceOnly);
  table[0x1c] = spec(CapcomSnesCommandKind::EchoOnOff, "Echo On/Off", SequenceSemantic::Meta);
  table[0x1d] = spec(CapcomSnesCommandKind::ReleaseRate, "Release Rate", SequenceSemantic::Meta,
                     CommandPlaybackStatus::SourceOnly);
  table[0x1e] = spec(CapcomSnesCommandKind::NoOperation, "No Operation", SequenceSemantic::Meta,
                     CommandPlaybackStatus::NoOp, "nop");
  table[0x1f] = table[0x1e];
  return table;
}();

struct OpcodePatch {
  u8 opcode = 0;
  CommandSpec replacement;
};

constexpr std::array kVersion1Patches{
    OpcodePatch{0x1e, spec(CapcomSnesCommandKind::UnknownOneByte, "Unknown One-Byte Event", SequenceSemantic::Meta,
                           CommandPlaybackStatus::SourceOnly, "unknown-one-byte")},
    OpcodePatch{0x1f, spec(CapcomSnesCommandKind::UnknownOneByte, "Unknown One-Byte Event", SequenceSemantic::Meta,
                           CommandPlaybackStatus::SourceOnly, "unknown-one-byte")},
};

[[nodiscard]] CommandSpec commandSpec(CapcomSnesEngineVersion version, u8 opcode) {
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    for (const auto& patch : kVersion1Patches) {
      if (patch.opcode == opcode) {
        return patch.replacement;
      }
    }
  }
  return kBaseOpcodeProfile[opcode];
}

constexpr CommandSpec kNoteSpec = spec(CapcomSnesCommandKind::Note, "Note", SequenceSemantic::Note);
constexpr CommandSpec kRestSpec = spec(CapcomSnesCommandKind::Rest, "Rest", SequenceSemantic::Rest);

namespace math {

constexpr std::array<u8, 17> kVolumeCurve{0x00, 0x0c, 0x19, 0x26, 0x33, 0x40, 0x4c, 0x59, 0x66,
                                          0x73, 0x80, 0x8c, 0x99, 0xb3, 0xcc, 0xe6, 0xff};
constexpr std::array<u8, 22> kPanCurve{0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29, 0x34, 0x42, 0x51,
                                       0x5e, 0x67, 0x6e, 0x73, 0x77, 0x7a, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f};
constexpr double kPiOverTwo = 1.57079632679489661923;
constexpr double kLfoStepHertz = 1000.0 / 16384.0;
constexpr double kVibratoBaseHertz = kLfoStepHertz;
constexpr double kVibratoMaxHertz = 255.0 * kLfoStepHertz;
constexpr double kTremoloMuteFloorCentibels = 960.0;
constexpr double kTremoloHalfDepthCentibels = 484.0;

struct Pan {
  double position = 0.0;
  double gain = 1.0;
};

[[nodiscard]] int interpolate(const auto& table, int index, int fraction) {
  const int lower = table[index];
  const int upper = table[index + 1];
  return lower + (((upper - lower) * fraction) >> 8);
}

[[nodiscard]] Pan panFromBalance(double sourceLeft, double sourceRight) {
  if (sourceLeft == 0.0 && sourceRight == 0.0) {
    return Pan{.gain = 0.0};
  }
  const double angle = std::atan2(sourceRight, sourceLeft);
  return Pan{
      .position = std::clamp((angle / kPiOverTwo) * 2.0 - 1.0, -1.0, 1.0),
      .gain = (sourceLeft + sourceRight) / (std::cos(angle) + std::sin(angle)),
  };
}

[[nodiscard]] double volumeGain(CapcomSnesEngineVersion version, u8 rawVolume) {
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    return rawVolume / 255.0;
  }
  if (rawVolume >= 0x80) {
    return 1.0;
  }
  const int index = rawVolume >> 3;
  const int fraction = ((rawVolume & 0x07) << 5) | 0x1f;
  return static_cast<double>(interpolate(kVolumeCurve, index, fraction)) / 255.0;
}

[[nodiscard]] double tuningCents(s8 tuning) {
  return static_cast<double>(tuning) * (100.0 / 256.0);
}

[[nodiscard]] double portamentoMillisecondsPerCent(u8 rawTime) {
  const u8 step = static_cast<u8>((rawTime << 1) & 0xff);
  const double centsPerUpdate = step * (100.0 / 256.0);
  return centsPerUpdate == 0.0 ? 0.0 : (0.016 / centsPerUpdate) * 1000.0;
}

[[nodiscard]] u32 baseNoteTicks(u32 rawDuration) {
  return rawDuration == 0 || rawDuration > 7 ? 0 : 192u >> (7u - rawDuration);
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u32 rawTempo) {
  return rawTempo == 0 ? 60000000 : static_cast<u32>(std::round(kCapcomSnesPpqn * (125 * 0x40) * 2 * 256.0 / rawTempo));
}

[[nodiscard]] Pan pan(CapcomSnesEngineVersion version, u8 rawPan) {
  const auto biasedPan = static_cast<u8>(rawPan + 0x80);
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    const double position = biasedPan == 255 ? 1.0 : biasedPan / 256.0;
    return panFromBalance(1.0 - position, position);
  }

  const u16 rightPosition = static_cast<u16>(biasedPan) * 20;
  const u16 leftPosition = 0x1400 - rightPosition;
  const double left = interpolate(kPanCurve, leftPosition >> 8, leftPosition & 0xff) / 128.0;
  const double right = interpolate(kPanCurve, rightPosition >> 8, rightPosition & 0xff) / 128.0;
  return panFromBalance(left, right);
}

[[nodiscard]] double normalizedDepth(u8 value) {
  return static_cast<double>(value) / 127.0;
}

[[nodiscard]] double tremoloDepth(CapcomSnesEngineVersion version, u8 rawDepth) {
  int trough = 0;
  int peak = 250;
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    const int depth = rawDepth & 0x7f;
    trough = depth == 0 ? 255 : 255 - ((2 * depth * 255) >> 8);
    peak = 255;
  } else if (rawDepth == 0) {
    trough = 250;
  } else if (rawDepth >= 127) {
    trough = 0;
  } else {
    const int inversePosition = 0x7e81 - rawDepth * 255;
    const int curvePosition = inversePosition >> 3;
    trough = interpolate(kVolumeCurve, curvePosition >> 8, curvePosition & 0xff);
  }

  double depthCentibels = kTremoloMuteFloorCentibels;
  if (trough > 0) {
    depthCentibels =
        std::clamp(200.0 * std::log10(peak / static_cast<double>(trough)), 0.0, kTremoloMuteFloorCentibels);
  }
  return std::clamp(depthCentibels / (2.0 * kTremoloHalfDepthCentibels), 0.0, 1.0);
}

[[nodiscard]] double lfoRate(u8 rawRate) {
  if (rawRate == 0) {
    return 0.0;
  }
  const auto cents = [](double hertz) { return 1200.0 * std::log2(hertz / 440.0) + 6900.0; };
  const double position = (cents(rawRate * kLfoStepHertz) - cents(kVibratoBaseHertz)) /
                          (cents(kVibratoMaxHertz) - cents(kVibratoBaseHertz));
  return std::clamp(position, 0.0, 1.0);
}

}  // namespace math

struct OperandSpec {
  std::string_view name;
  SourceValueDisplay display = SourceValueDisplay::Default;
  SemanticOperandRole role = SemanticOperandRole::Value;
};

[[nodiscard]] constexpr OperandSpec operandSpec(CapcomSnesOperand id) {
  switch (id) {
    case CapcomSnesOperand::KeyIndex:
      return {"key_index", SourceValueDisplay::Decimal, SemanticOperandRole::NoteKey};
    case CapcomSnesOperand::DurationIndex:
      return {"duration_index", SourceValueDisplay::Decimal, SemanticOperandRole::Duration};
    case CapcomSnesOperand::TempoMicrosecondsPerQuarter:
      return {"microseconds_per_quarter", SourceValueDisplay::Decimal};
    case CapcomSnesOperand::LinearGain:
      return {"linear_gain", SourceValueDisplay::Default, SemanticOperandRole::Level};
    case CapcomSnesOperand::StereoPosition:
      return {"stereo_position", SourceValueDisplay::Default, SemanticOperandRole::Pan};
    case CapcomSnesOperand::TuningCents:
      return {"cents", SourceValueDisplay::Cents, SemanticOperandRole::Pitch};
    case CapcomSnesOperand::PortamentoMillisecondsPerCent:
      return {"milliseconds_per_cent", SourceValueDisplay::Default, SemanticOperandRole::Duration};
    case CapcomSnesOperand::Enabled:
      return {"enabled", SourceValueDisplay::Boolean, SemanticOperandRole::State};
    case CapcomSnesOperand::ReleaseGain:
      return {"gain", SourceValueDisplay::Hex, SemanticOperandRole::State};
    case CapcomSnesOperand::Attributes:
      return {"attributes", SourceValueDisplay::Hex, SemanticOperandRole::State};
    case CapcomSnesOperand::Rate:
      return {"rate", SourceValueDisplay::Decimal, SemanticOperandRole::State};
    case CapcomSnesOperand::Instrument:
      return {"instrument", SourceValueDisplay::Decimal, SemanticOperandRole::Instrument};
    case CapcomSnesOperand::Octave:
      return {"octave", SourceValueDisplay::Decimal, SemanticOperandRole::Pitch};
    case CapcomSnesOperand::Semitones:
      return {"semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch};
    case CapcomSnesOperand::Slot:
      return {"slot", SourceValueDisplay::Decimal, SemanticOperandRole::State};
    case CapcomSnesOperand::Count:
      return {"count", SourceValueDisplay::Decimal, SemanticOperandRole::Count};
    case CapcomSnesOperand::Destination:
      return {"destination", SourceValueDisplay::Address, SemanticOperandRole::Address};
    case CapcomSnesOperand::Type:
      return {"type", SourceValueDisplay::Decimal, SemanticOperandRole::Modulation};
    case CapcomSnesOperand::Value:
      return {"value", SourceValueDisplay::Hex};
    case CapcomSnesOperand::Argument:
      return {"argument", SourceValueDisplay::Hex};
    case CapcomSnesOperand::Preset:
      return {"preset", SourceValueDisplay::Hex};
  }
  return {"value"};
}

void addOperand(std::vector<SemanticOperand>& operands, CapcomSnesOperand id, SemanticOperandValue value,
                SourceRange range = {}, std::optional<SemanticOperandValue> encodedValue = std::nullopt,
                std::string_view encodedName = {}, SourceValueDisplay encodedDisplay = SourceValueDisplay::Default,
                std::optional<SemanticOperandRole> role = std::nullopt) {
  const auto spec = operandSpec(id);
  operands.push_back(SemanticOperand{
      .id = operandId(id),
      .value = std::move(value),
      .range = range,
      .name = std::string(spec.name),
      .display = spec.display,
      .role = role.value_or(spec.role),
      .encodedValue = std::move(encodedValue),
      .encodedName = std::string(encodedName),
      .encodedDisplay = encodedDisplay,
  });
}

void addUnsigned(std::vector<SemanticOperand>& operands, CapcomSnesOperand id, u64 value, SourceRange range = {}) {
  addOperand(operands, id, SemanticOperandValue{value}, range);
}

void addSigned(std::vector<SemanticOperand>& operands, CapcomSnesOperand id, s64 value, SourceRange range = {}) {
  addOperand(operands, id, SemanticOperandValue{value}, range);
}

void addDouble(std::vector<SemanticOperand>& operands, CapcomSnesOperand id, double value, SourceRange range = {}) {
  addOperand(operands, id, SemanticOperandValue{value}, range);
}

void addResolved(std::vector<SemanticOperand>& operands, CapcomSnesOperand id, SemanticOperandValue value,
                 SemanticOperandValue encodedValue, SourceRange range, std::string_view encodedName,
                 SourceValueDisplay encodedDisplay = SourceValueDisplay::Default) {
  addOperand(operands, id, std::move(value), range, std::move(encodedValue), encodedName, encodedDisplay);
}

void addAddress(std::vector<SemanticOperand>& operands, CapcomSnesOperand id, Address value, SourceRange range = {},
                SemanticOperandRole role = SemanticOperandRole::Address) {
  addOperand(operands, id, SemanticOperandValue{value}, range, std::nullopt, {}, SourceValueDisplay::Default, role);
}

template <class T>
[[nodiscard]] T operand(const SourceCommand& command, CapcomSnesOperand id) {
  const SemanticOperand* found = semanticOperand(command, operandId(id));
  if (found == nullptr) {
    throw std::logic_error("CapcomSnes semantic command is missing an operand");
  }
  return std::get<T>(found->value);
}

template <class T>
[[nodiscard]] T decodedOperand(const std::vector<SemanticOperand>& operands, CapcomSnesOperand id) {
  const auto found =
      std::ranges::find_if(operands, [id](const SemanticOperand& value) { return value.id == operandId(id); });
  if (found == operands.end()) {
    throw std::logic_error("CapcomSnes decoded command is missing an operand");
  }
  return std::get<T>(found->value);
}

[[nodiscard]] u8 unsigned8(const SourceCommand& command, CapcomSnesOperand id) {
  return static_cast<u8>(operand<u64>(command, id));
}

[[nodiscard]] s8 signed8(const SourceCommand& command, CapcomSnesOperand id) {
  return static_cast<s8>(operand<s64>(command, id));
}

[[nodiscard]] DecodedCommandPresentation presentation(const CommandSpec& command) {
  const std::string localKind = sourceLocalKind(command.localKind);
  return DecodedCommandPresentation{
      .label = std::string(command.name),
      .localKind = localKind,
      .detailKind = "capcom-snes." + localKind,
      .semantic = command.semantic,
      .playback = command.playback,
  };
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end,
                                                   CapcomSnesEngineVersion version,
                                                   std::vector<Diagnostic>* diagnostics) {
  RecordReader record(reader, begin, end, diagnostics);
  const auto opcodeValue = record.u8("opcode", SourceValueDisplay::Hex);
  if (!opcodeValue) {
    const auto truncated = spec(CapcomSnesCommandKind::Unsupported, "Truncated Command", SequenceSemantic::Unsupported,
                                CommandPlaybackStatus::Unsupported, "truncated");
    return DecodedBytecodeCommand{
        .range = record.range(),
        .opcode = 0,
        .encodedSize = std::max<u32>(1, record.size()),
        .flow = DecodeFlow::terminalFlow(),
        .kind = commandKind(CapcomSnesCommandKind::Unsupported),
        .presentation = presentation(truncated),
        .retainBytes = false,
    };
  }

  const u8 opcode = *opcodeValue;
  CommandSpec decodedSpec =
      opcode >= 0x20 ? ((opcode & 0x1f) == 0 ? kRestSpec : kNoteSpec) : commandSpec(version, opcode);
  std::vector<SemanticOperand> operands;
  DecodeFlow flow;

  if (opcode >= 0x20) {
    const u8 durationIndex = opcode >> 5;
    addUnsigned(operands, CapcomSnesOperand::DurationIndex, durationIndex, opcodeValue.range);
    if ((opcode & 0x1f) != 0) {
      const u8 keyIndex = opcode & 0x1f;
      addUnsigned(operands, CapcomSnesOperand::KeyIndex, keyIndex, opcodeValue.range);
    }
  } else {
    switch (decodedSpec.kind) {
      case CapcomSnesCommandKind::NoteAttributes: {
        const auto value = record.u8("raw");
        addUnsigned(operands, CapcomSnesOperand::Attributes, *value, value.range);
        break;
      }
      case CapcomSnesCommandKind::Tempo: {
        const auto value = record.u16be("raw");
        addResolved(operands, CapcomSnesOperand::TempoMicrosecondsPerQuarter,
                    SemanticOperandValue{static_cast<u64>(math::tempoMicrosecondsPerQuarter(*value))},
                    SemanticOperandValue{static_cast<u64>(*value)}, value.range, "raw");
        break;
      }
      case CapcomSnesCommandKind::DurationRate: {
        const auto value = record.u8("rate");
        addUnsigned(operands, CapcomSnesOperand::Rate, *value, value.range);
        break;
      }
      case CapcomSnesCommandKind::Volume:
      case CapcomSnesCommandKind::MasterVolume: {
        const auto value = record.u8("raw");
        addResolved(operands, CapcomSnesOperand::LinearGain, SemanticOperandValue{math::volumeGain(version, *value)},
                    SemanticOperandValue{static_cast<u64>(*value)}, value.range, "raw");
        break;
      }
      case CapcomSnesCommandKind::Instrument: {
        const auto value = record.u8("instrument");
        addUnsigned(operands, CapcomSnesOperand::Instrument, *value, value.range);
        break;
      }
      case CapcomSnesCommandKind::Octave: {
        const auto value = record.u8("octave");
        addUnsigned(operands, CapcomSnesOperand::Octave, *value, value.range);
        break;
      }
      case CapcomSnesCommandKind::GlobalTranspose:
      case CapcomSnesCommandKind::Transpose: {
        const auto value = record.s8("semitones");
        addSigned(operands, CapcomSnesOperand::Semitones, *value, value.range);
        break;
      }
      case CapcomSnesCommandKind::Tuning: {
        const auto value = record.s8("tuning");
        addResolved(operands, CapcomSnesOperand::TuningCents, SemanticOperandValue{math::tuningCents(*value)},
                    SemanticOperandValue{static_cast<s64>(*value)}, value.range, "tuning",
                    SourceValueDisplay::SignedDecimal);
        break;
      }
      case CapcomSnesCommandKind::PortamentoTime: {
        const auto value = record.u8("time");
        addResolved(operands, CapcomSnesOperand::PortamentoMillisecondsPerCent,
                    SemanticOperandValue{math::portamentoMillisecondsPerCent(*value)},
                    SemanticOperandValue{static_cast<u64>(*value)}, value.range, "time");
        break;
      }
      case CapcomSnesCommandKind::RepeatUntil: {
        const u8 slot = opcode - 0x0e;
        addUnsigned(operands, CapcomSnesOperand::Slot, slot + 1);
        const auto count = record.u8("count");
        const auto destination = record.u16be("destination", SourceValueDisplay::Address);
        addUnsigned(operands, CapcomSnesOperand::Count, *count, count.range);
        addAddress(operands, CapcomSnesOperand::Destination, Address{*destination}, destination.range,
                   SemanticOperandRole::RepeatTarget);
        break;
      }
      case CapcomSnesCommandKind::RepeatBreak: {
        const u8 slot = opcode - 0x12;
        addUnsigned(operands, CapcomSnesOperand::Slot, slot + 1);
        const auto attributes = record.u8("attributes");
        const auto destination = record.u16be("destination", SourceValueDisplay::Address);
        addUnsigned(operands, CapcomSnesOperand::Attributes, *attributes, attributes.range);
        addAddress(operands, CapcomSnesOperand::Destination, Address{*destination}, destination.range,
                   SemanticOperandRole::RepeatTarget);
        break;
      }
      case CapcomSnesCommandKind::Jump: {
        const auto destination = record.u16be("destination", SourceValueDisplay::Address);
        addAddress(operands, CapcomSnesOperand::Destination, Address{*destination}, destination.range,
                   SemanticOperandRole::JumpTarget);
        break;
      }
      case CapcomSnesCommandKind::Pan: {
        const auto value = record.u8("raw");
        const auto converted = math::pan(version, *value);
        addResolved(operands, CapcomSnesOperand::StereoPosition, SemanticOperandValue{converted.position},
                    SemanticOperandValue{static_cast<u64>(*value)}, value.range, "raw");
        addDouble(operands, CapcomSnesOperand::LinearGain, converted.gain);
        break;
      }
      case CapcomSnesCommandKind::Lfo: {
        const auto type = record.u8("type");
        const auto value = record.u8("value");
        addUnsigned(operands, CapcomSnesOperand::Type, *type, type.range);
        addUnsigned(operands, CapcomSnesOperand::Value, *value, value.range);
        break;
      }
      case CapcomSnesCommandKind::EchoParam: {
        const auto argument = record.u8("argument");
        const auto preset = record.u8("preset");
        addUnsigned(operands, CapcomSnesOperand::Argument, *argument, argument.range);
        addUnsigned(operands, CapcomSnesOperand::Preset, *preset, preset.range);
        break;
      }
      case CapcomSnesCommandKind::EchoOnOff:
      case CapcomSnesCommandKind::ReleaseRate: {
        const auto value = record.u8("raw");
        if (decodedSpec.kind == CapcomSnesCommandKind::EchoOnOff) {
          addResolved(operands, CapcomSnesOperand::Enabled, SemanticOperandValue{(*value & 1) != 0},
                      SemanticOperandValue{static_cast<u64>(*value)}, value.range, "raw");
        } else {
          addResolved(operands, CapcomSnesOperand::ReleaseGain, SemanticOperandValue{static_cast<u64>(*value | 0xa0)},
                      SemanticOperandValue{static_cast<u64>(*value)}, value.range, "raw");
        }
        break;
      }
      case CapcomSnesCommandKind::UnknownOneByte: {
        const auto value = record.u8("value");
        addUnsigned(operands, CapcomSnesOperand::Value, *value, value.range);
        break;
      }
      default:
        break;
    }
  }

  if (!record.ok()) {
    decodedSpec = spec(CapcomSnesCommandKind::Unsupported, "Truncated Command", SequenceSemantic::Unsupported,
                       CommandPlaybackStatus::Unsupported, "truncated");
    flow = DecodeFlow::terminalFlow();
  } else {
    const Address next{record.position()};
    switch (decodedSpec.kind) {
      case CapcomSnesCommandKind::End:
      case CapcomSnesCommandKind::Unsupported:
        flow = DecodeFlow::terminalFlow();
        break;
      case CapcomSnesCommandKind::Jump:
        flow = DecodeFlow::jump(decodedOperand<Address>(operands, CapcomSnesOperand::Destination));
        break;
      case CapcomSnesCommandKind::RepeatUntil: {
        const auto destination = decodedOperand<Address>(operands, CapcomSnesOperand::Destination);
        if (decodedOperand<u64>(operands, CapcomSnesOperand::Count) == 0) {
          flow = DecodeFlow::jump(destination);
        } else {
          flow = DecodeFlow{.kind = DecodeFlow::Kind::Fallthrough, .fallthrough = next, .staticTargets = {destination}};
        }
        break;
      }
      case CapcomSnesCommandKind::RepeatBreak:
        flow = DecodeFlow{.kind = DecodeFlow::Kind::Fallthrough,
                          .fallthrough = next,
                          .staticTargets = {decodedOperand<Address>(operands, CapcomSnesOperand::Destination)}};
        break;
      default:
        flow = DecodeFlow::fallthroughTo(next);
        break;
    }
  }

  return DecodedBytecodeCommand{
      .range = record.range(),
      .opcode = opcode,
      .encodedSize = record.size(),
      .flow = std::move(flow),
      .kind = commandKind(decodedSpec.kind),
      .operands = std::move(operands),
      .presentation = presentation(decodedSpec),
      .retainBytes = false,
  };
}

struct ProgramState {
  s32 globalTranspose = 0;
};

struct TrackState {
  CapcomSnesEngineVersion version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  u32 durationRate = 0;
  s32 transpose = 0;
  u32 noteOctave = 0;
  bool noteDotted = false;
  bool noteTriplet = false;
  bool noteSlurred = false;
  bool noteOctaveUp = false;
  u8 modulationRate = 0;
  double vibratoDepth = 0.0;
  double tremoloDepth = 0.0;
  double portamentoMillisecondsPerCent = 0.0;
  u16 lastPortamentoTime = 0;
  std::optional<s32> lastSourceKey;
  bool lastNoteSlurred = false;
  bool didRest = false;

  [[nodiscard]] u32 consumeNoteTicks(u8 rawDuration) {
    u32 length = math::baseNoteTicks(rawDuration);
    if (noteDotted) {
      length = (length % 2 == 0 && length < 0x80) ? length + (length / 2) : 0;
      noteDotted = false;
    } else if (noteTriplet) {
      length = length * 2 / 3;
    }
    return length;
  }

  [[nodiscard]] u32 soundingTicks(u32 length) const {
    u32 duration = length * durationRate;
    if (noteSlurred || duration == 0) {
      duration = length << 8;
    }
    duration = (duration + 0x80) >> 8;
    return duration == 0 ? 1 : duration;
  }

  [[nodiscard]] s32 sourceKey(u8 keyIndex) const {
    return static_cast<s32>(keyIndex) - 1 + static_cast<s32>(noteOctave * 12) + (noteOctaveUp ? 24 : 0);
  }

  void applyAttributes(u8 attributes, PerformanceEmitter& out) {
    const bool wasSlurred = noteSlurred;
    noteOctave |= attributes & kNoteOctaveMask;
    noteDotted = noteDotted || ((attributes & kNoteDottedMask) != 0);
    noteOctaveUp = (attributes & kNoteOctaveUpMask) != 0;
    noteTriplet = (attributes & kNoteTripletMask) != 0;
    noteSlurred = (attributes & kNoteSlurredMask) != 0;
    if (noteSlurred != wasSlurred) {
      out.legatoPedal(noteSlurred);
    }
  }

  void emitModulationDepths(PerformanceEmitter& out, bool enabled) const {
    if (vibratoDepth != 0) {
      out.modulation(ModulationPerformanceTarget::VibratoDepth, enabled ? vibratoDepth : 0.0);
    }
    if (tremoloDepth != 0) {
      out.modulation(ModulationPerformanceTarget::TremoloDepth, enabled ? tremoloDepth : 0.0);
    }
  }
};

[[nodiscard]] std::any createProgramState(const SequenceProgram&) {
  return ProgramState{};
}

[[nodiscard]] std::any createTrackState(const SequenceProgram& program, const TrackProgram&) {
  return TrackState{.version = versionFrom(program)};
}

[[nodiscard]] Effects executeCommand(const SourceCommand& command, std::any& programStateValue,
                                     std::any& trackStateValue, PerformanceEmitter& out, VmApi& vm) {
  auto& programState = std::any_cast<ProgramState&>(programStateValue);
  auto& state = std::any_cast<TrackState&>(trackStateValue);
  const auto kind = static_cast<CapcomSnesCommandKind>(command.kind.value);

  switch (kind) {
    case CapcomSnesCommandKind::Note:
    case CapcomSnesCommandKind::Rest: {
      const u32 length = state.consumeNoteTicks(unsigned8(command, CapcomSnesOperand::DurationIndex));
      if (kind == CapcomSnesCommandKind::Rest) {
        state.didRest = true;
        return Effects::wait(length);
      }

      const s32 key = state.sourceKey(unsigned8(command, CapcomSnesOperand::KeyIndex));
      const u32 duration = state.soundingTicks(length);
      if (state.lastNoteSlurred && state.lastSourceKey && key == *state.lastSourceKey && !state.didRest) {
        out.note(static_cast<double>(key + state.transpose), 1.0, duration, true);
        state.lastNoteSlurred = state.noteSlurred;
        return Effects::wait(length);
      }

      if (state.portamentoMillisecondsPerCent > 0.0 && state.lastSourceKey) {
        const auto distance = static_cast<u32>(std::abs(key - *state.lastSourceKey));
        const auto portamentoTime = static_cast<u16>(distance * 100 * state.portamentoMillisecondsPerCent);
        if (portamentoTime != state.lastPortamentoTime) {
          out.portamento(static_cast<double>(portamentoTime),
                         static_cast<double>(*state.lastSourceKey + state.transpose));
          state.lastPortamentoTime = portamentoTime;
        } else {
          out.portamentoControl(static_cast<double>(*state.lastSourceKey + state.transpose));
        }
      }
      out.note(static_cast<double>(key + state.transpose), 1.0, duration + (state.noteSlurred ? 1u : 0u));
      state.lastSourceKey = key;
      state.didRest = false;
      state.lastNoteSlurred = state.noteSlurred;
      return Effects::wait(length);
    }

    case CapcomSnesCommandKind::ToggleTriplet:
      state.noteTriplet = !state.noteTriplet;
      break;
    case CapcomSnesCommandKind::ToggleSlur:
      state.noteSlurred = !state.noteSlurred;
      out.legatoPedal(state.noteSlurred);
      break;
    case CapcomSnesCommandKind::DottedNote:
      state.noteDotted = true;
      break;
    case CapcomSnesCommandKind::ToggleOctaveUp:
      state.noteOctaveUp = !state.noteOctaveUp;
      break;
    case CapcomSnesCommandKind::NoteAttributes:
      state.applyAttributes(unsigned8(command, CapcomSnesOperand::Attributes), out);
      break;
    case CapcomSnesCommandKind::Tempo:
      out.tempo(static_cast<u32>(operand<u64>(command, CapcomSnesOperand::TempoMicrosecondsPerQuarter)));
      break;
    case CapcomSnesCommandKind::DurationRate:
      state.durationRate = unsigned8(command, CapcomSnesOperand::Rate);
      break;
    case CapcomSnesCommandKind::Volume:
      out.level(LevelScale::linearFromLinear(operand<double>(command, CapcomSnesOperand::LinearGain)),
                ValueQuantization{.levels = 256});
      break;
    case CapcomSnesCommandKind::Instrument:
      out.instrument(InstrumentIdentity{
          .domain = std::string(kCapcomSnesInstrumentDomain),
          .key = static_cast<u32>(operand<u64>(command, CapcomSnesOperand::Instrument)),
      });
      break;
    case CapcomSnesCommandKind::Octave:
      state.noteOctave = unsigned8(command, CapcomSnesOperand::Octave);
      break;
    case CapcomSnesCommandKind::GlobalTranspose:
      programState.globalTranspose = signed8(command, CapcomSnesOperand::Semitones);
      out.globalTranspose(programState.globalTranspose);
      break;
    case CapcomSnesCommandKind::Transpose:
      state.transpose = signed8(command, CapcomSnesOperand::Semitones);
      break;
    case CapcomSnesCommandKind::Tuning:
      out.tuning(operand<double>(command, CapcomSnesOperand::TuningCents));
      break;
    case CapcomSnesCommandKind::PortamentoTime:
      state.portamentoMillisecondsPerCent = operand<double>(command, CapcomSnesOperand::PortamentoMillisecondsPerCent);
      break;
    case CapcomSnesCommandKind::RepeatUntil: {
      const u8 slot = unsigned8(command, CapcomSnesOperand::Slot) - 1;
      const u32 count = static_cast<u32>(operand<u64>(command, CapcomSnesOperand::Count));
      const Address destination = operand<Address>(command, CapcomSnesOperand::Destination);
      return count == 0 ? Effects{.step = vm.declaredLoop(destination)}
                        : vm.countedRepeatUntil(slot, count + 1, destination);
    }
    case CapcomSnesCommandKind::RepeatBreak: {
      const auto branch = vm.countedRepeatBreak(unsigned8(command, CapcomSnesOperand::Slot) - 1,
                                                operand<Address>(command, CapcomSnesOperand::Destination));
      if (branch.taken) {
        state.applyAttributes(unsigned8(command, CapcomSnesOperand::Attributes), out);
      }
      return branch.effects;
    }
    case CapcomSnesCommandKind::Jump:
      return Effects{.step = vm.loopCandidate(operand<Address>(command, CapcomSnesOperand::Destination))};
    case CapcomSnesCommandKind::End:
    case CapcomSnesCommandKind::Unsupported:
      return Effects{.step = vm.end()};
    case CapcomSnesCommandKind::Pan: {
      out.pan(operand<double>(command, CapcomSnesOperand::StereoPosition),
              LevelScale::linearFromLinear(operand<double>(command, CapcomSnesOperand::LinearGain)));
      break;
    }
    case CapcomSnesCommandKind::MasterVolume:
      out.masterLevel(LevelScale::linearFromLinear(operand<double>(command, CapcomSnesOperand::LinearGain)));
      break;
    case CapcomSnesCommandKind::Lfo: {
      const u8 type = unsigned8(command, CapcomSnesOperand::Type);
      const u8 value = unsigned8(command, CapcomSnesOperand::Value);
      switch (type) {
        case 0:
          state.vibratoDepth = math::normalizedDepth(value & 0x7f);
          out.modulation(ModulationPerformanceTarget::VibratoDepth,
                         state.modulationRate != 0 ? state.vibratoDepth : 0.0);
          break;
        case 1:
          state.tremoloDepth = math::tremoloDepth(state.version, value);
          out.modulation(ModulationPerformanceTarget::TremoloDepth,
                         state.modulationRate != 0 ? state.tremoloDepth : 0.0);
          break;
        case 2: {
          const bool wasEnabled = state.modulationRate != 0;
          state.modulationRate = value;
          const bool enabled = state.modulationRate != 0;
          if (enabled != wasEnabled) {
            state.emitModulationDepths(out, enabled);
          }
          const double rate = math::lfoRate(value);
          out.modulation(ModulationPerformanceTarget::VibratoRate, rate);
          out.modulation(ModulationPerformanceTarget::TremoloRate, rate);
          break;
        }
        default:
          break;
      }
      break;
    }
    case CapcomSnesCommandKind::EchoOnOff:
      out.reverb(operand<bool>(command, CapcomSnesOperand::Enabled) ? 40.0 / 127.0 : 0.0);
      break;
    case CapcomSnesCommandKind::EchoParam:
    case CapcomSnesCommandKind::ReleaseRate:
    case CapcomSnesCommandKind::UnknownOneByte:
    case CapcomSnesCommandKind::NoOperation:
      break;
  }
  return Effects::none();
}

[[nodiscard]] SequenceDialect makeDialect() {
  return SequenceDialect{
      .id = DialectId{.value = "capcom-snes"},
      .commandDetailKindPrefix = "capcom-snes",
      .timebase = Timebase{.ppqn = kCapcomSnesPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .initialReverbSend = 0.0,
              .initialMonoModeChannels = 0,
          },
      .createProgramState = createProgramState,
      .createSemanticTrackState = createTrackState,
      .executeSemantic = executeCommand,
  };
}

[[nodiscard]] SourceRange trackRange(ByteReader reader, const TrackProgram& track, u32 fallback) {
  if (track.commands.empty()) {
    return reader.range(fallback, 0);
  }
  u64 begin = track.commands.front().range.offset;
  u64 end = track.commands.front().range.endOffset();
  for (const auto& command : track.commands) {
    begin = std::min(begin, command.range.offset);
    end = std::max(end, command.range.endOffset());
  }
  return reader.range(begin, end - begin);
}

}  // namespace

const SequenceDialect& capcomSnesSequenceDialect() {
  static const SequenceDialect dialect = makeDialect();
  return dialect;
}

TrackProgram decodeCapcomSnesSourceTrack(ByteReader reader, CapcomSnesEngineVersion version, u32 sourceTrackNumber,
                                         u32 startAddress, SourceMapBuilder* sourceMap,
                                         std::vector<Diagnostic>* diagnostics,
                                         std::optional<SourceAnnotationId> parentAnnotation,
                                         std::optional<AssetId> sequenceAsset) {
  std::optional<SourceAnnotationId> trackAnnotation;
  if (sourceMap != nullptr) {
    auto annotation = sourceMap
                          ->annotation(SourceRole::SequenceTrack, "Track " + std::to_string(sourceTrackNumber),
                                       reader.range(startAddress, 0))
                          .kind("track");
    if (sequenceAsset) {
      annotation.owner(ObjectRefs::sequenceTrack(*sequenceAsset, sourceTrackNumber));
    }
    if (parentAnnotation) {
      annotation.parent(*parentAnnotation);
    }
    trackAnnotation = annotation.id();
  }

  const u32 end = static_cast<u32>(reader.size());
  auto track = decodeLinearBytecodeTrack(
      reader, sourceTrackNumber, startAddress, LinearBytecodeDecodePolicy{.maxCommands = 4096}, [&](u32 offset) {
        auto command = decodeCommand(reader, offset, end, version, diagnostics);
        command.annotation = projectDecodedCommand(sourceMap, command, trackAnnotation);
        return command;
      });
  if (sourceMap != nullptr && trackAnnotation) {
    AnnotationBuilder{*sourceMap, *trackAnnotation}.range(trackRange(reader, track, startAddress));
  }
  return track;
}

SequenceProgramAsset parseCapcomSnesSequence(const ScanInput& input, const CapcomSnesLayout& layout, AssetId sequenceId,
                                             std::string_view displayName, SourceMapBuilder* sourceMap,
                                             std::vector<Diagnostic>* diagnostics) {
  const u32 headerSize = (layout.priorityInHeader ? 1 : 0) + kCapcomSnesMaxTracks * 2;
  const SourceRange headerRange = input.reader.range(layout.sequenceHeaderAddress, headerSize);

  SourceAnnotationId headerAnnotation;
  if (sourceMap != nullptr) {
    headerAnnotation = sourceMap->header("Sequence Header", headerRange)
                           .kind("capcom-snes-sequence-header")
                           .owner(ObjectRefs::sequence(sequenceId))
                           .id();
  }

  const auto& dialect = capcomSnesSequenceDialect();
  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = profileFor(layout.version)},
      .behavior = dialect.defaultBehavior,
  };
  const u32 pointerBase = layout.sequenceHeaderAddress + (layout.priorityInHeader ? 1 : 0);
  for (u32 pointerIndex = kCapcomSnesMaxTracks; pointerIndex-- > 0;) {
    const u32 sourceTrackNumber = kCapcomSnesMaxTracks - 1 - pointerIndex;
    const u32 pointerOffset = pointerBase + pointerIndex * 2;
    const SourceRange pointerRange = input.reader.range(pointerOffset, 2);
    const u16 trackAddress = input.reader.be16(pointerOffset);
    if (trackAddress == 0) {
      continue;
    }

    std::optional<SourceAnnotationId> pointerAnnotation;
    if (sourceMap != nullptr) {
      auto annotation =
          sourceMap->pointer("Track Pointer", pointerRange, SourceTarget{input.reader.range(trackAddress, 1)})
              .kind("capcom-snes-track-pointer")
              .description(fmt::format("Track starts at ${:04X}", trackAddress))
              .derived("source_track", sourceTrackNumber)
              .field("destination", pointerRange, trackAddress, SourceValueDisplay::Address);
      if (headerAnnotation.valid()) {
        annotation.parent(headerAnnotation);
      }
      pointerAnnotation = annotation.id();
    }

    program.tracks.push_back(decodeCapcomSnesSourceTrack(input.reader, layout.version, sourceTrackNumber, trackAddress,
                                                         sourceMap, diagnostics, pointerAnnotation, sequenceId));
  }

  return SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = sequenceId,
              .format = "CapcomSnes",
              .name = std::string(displayName),
              .range = headerRange,
          },
      .program = std::move(program),
  };
}

}  // namespace vgmtrans::formats::capcom_snes
