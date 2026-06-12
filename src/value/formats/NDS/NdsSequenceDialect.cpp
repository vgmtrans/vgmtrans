/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequenceDialect.h"

#include "value/core/BytecodeSequenceDecoder.h"
#include "value/core/SequenceVm.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

#define NDS_KIND(Suffix, DisplayName)                     \
  static constexpr std::string_view kind = "nds." Suffix; \
  static constexpr std::string_view name = DisplayName

#define NDS_COMMAND(Op, Suffix, DisplayName) \
  static constexpr u8 opcode = Op;           \
  NDS_KIND(Suffix, DisplayName)

#define NDS_IGNORED_COMMAND(Op, Type, Suffix, DisplayName, OperandBytes) \
  struct Type : RawBytesOperand<Type, OperandBytes> {                    \
    NDS_COMMAND(Op, Suffix, DisplayName);                                \
  }

constexpr size_t kMaxTrackCommands = 262144;

struct Context {};

struct TrackState {
  explicit TrackState(const SequenceProgram& program, const TrackProgram&)
      : sequenceDataBase(program.sourceBaseAddress.value) {}

  u64 sequenceDataBase = 0;
  bool noteWait = false;
  s32 transpose = 0;
};

using Runtime = CommandRuntime<TrackState, Context>;

[[nodiscard]] u32 absoluteAddress(const TrackState& state, u32 relative) {
  return static_cast<u32>(state.sequenceDataBase + relative);
}

[[nodiscard]] u32 readVarLen(ByteReader reader, u32& offset, u32 sequenceEnd) {
  u32 value = 0;
  while (hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
    const u8 byte = reader.u8At(offset++);
    value = (value << 7) + (byte & 0x7f);
    if ((byte & 0x80) == 0) {
      break;
    }
  }
  return value;
}

[[nodiscard]] std::optional<u32> readSseqAddress(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd,
                                                 u32 operandOffset) {
  if (!hasBytecodeBytes(reader, operandOffset, 3, sequenceEnd)) {
    return std::nullopt;
  }
  return reader.u8At(operandOffset) + (reader.u8At(operandOffset + 1) << 8) + (reader.u8At(operandOffset + 2) << 16) +
         sequenceOffset + 0x1c;
}

struct Note {
  static constexpr u8 firstOpcode = 0x00;
  static constexpr u8 lastOpcode = 0x7f;
  u8 key = 0;
  u8 velocity = 0;
  u32 duration = 0;

  NDS_KIND("note", "Note");

  static Note parse(CommandReader& in) {
    in.derived("key", static_cast<u64>(in.opcode()));
    return Note{
        .key = in.opcode(),
        .velocity = in.u8("velocity"),
        .duration = in.varLen("duration"),
    };
  }

  Effects execute(Runtime& rt) const {
    rt.out.note(static_cast<double>(std::clamp<s32>(static_cast<s32>(key) + rt.state.transpose, 0, 127)),
                velocity / 127.0, duration);
    return rt.wait(rt.state.noteWait ? duration : 0);
  }
};

struct Rest {
  u32 duration = 0;

  NDS_COMMAND(0x80, "rest", "Rest");

  static Rest parse(CommandReader& in) { return Rest{.duration = in.varLen("duration")}; }

  Effects execute(Runtime& rt) const { return rt.wait(duration); }
};

struct Program {
  u32 raw = 0;

  NDS_COMMAND(0x81, "program", "Program");

  static Program parse(CommandReader& in) { return Program{.raw = in.varLen("program")}; }

  [[nodiscard]] u32 bank() const { return raw >> 7; }
  [[nodiscard]] u32 program() const { return raw & 0x7f; }

  void describe(CommandInfo& out) const {
    out.field("bank", bank());
    out.field("program", program());
  }

  void execute(Runtime& rt) const { rt.out.instrument(bank(), program()); }
};

template <class Derived>
struct RelativeAddressCommand {
  u32 relative = 0;

  static Derived parse(CommandReader& in) {
    Derived result;
    result.relative = in.le24("destination");
    return result;
  }
};

struct Jump : RelativeAddressCommand<Jump> {
  NDS_COMMAND(0x94, "jump", "Jump");

  Effects execute(Runtime& rt) const { return rt.jump(Address{absoluteAddress(rt.state, relative)}); }
};

struct Call : RelativeAddressCommand<Call> {
  NDS_COMMAND(0x95, "call", "Call");

  Effects execute(Runtime& rt) const { return rt.call(Address{absoluteAddress(rt.state, relative)}); }
};

struct Return : NoOperands<Return> {
  NDS_COMMAND(0xfd, "return", "Return");

  Effects execute(Runtime& rt) const { return rt.return_(); }
};

struct End : NoOperands<End> {
  NDS_COMMAND(0xff, "end", "End");

  Effects execute(Runtime& rt) const { return rt.end(); }
};

struct Pan : U8Operand<Pan> {
  NDS_COMMAND(0xc0, "pan", "Pan");
  static constexpr std::string_view operandName = "pan";

  void execute(Runtime& rt) const { rt.out.pan(std::clamp((static_cast<double>(raw) / 63.5) - 1.0, -1.0, 1.0)); }
};

struct Volume : U8Operand<Volume> {
  NDS_COMMAND(0xc1, "volume", "Volume");
  static constexpr std::string_view operandName = "volume";

  void execute(Runtime& rt) const { rt.out.level(std::clamp(static_cast<double>(raw) / 127.0, 0.0, 1.0)); }
};

struct ExpressionLevel : U8Operand<ExpressionLevel> {
  NDS_COMMAND(0xd5, "expression", "Expression");
  static constexpr std::string_view operandName = "expression";

  void execute(Runtime& rt) const { rt.out.expression(std::clamp(static_cast<double>(raw) / 127.0, 0.0, 1.0)); }
};

struct Transpose {
  s8 semitones = 0;

  NDS_COMMAND(0xc3, "transpose", "Transpose");

  static Transpose parse(CommandReader& in) { return Transpose{.semitones = in.s8("semitones")}; }

  void execute(Runtime& rt) const { rt.state.transpose = semitones; }
};

struct PitchBend : S8Operand<PitchBend> {
  NDS_COMMAND(0xc4, "pitch-bend", "Pitch Bend");
  static constexpr std::string_view operandName = "bend";

  void execute(Runtime& rt) const { rt.out.pitchBend(static_cast<s16>(raw * 64)); }
};

struct PitchBendRange : U8Operand<PitchBendRange> {
  NDS_COMMAND(0xc5, "pitch-bend-range", "Pitch Bend Range");
  static constexpr std::string_view operandName = "semitones";

  void execute(Runtime& rt) const { rt.out.pitchBendRange(raw); }
};

struct ModulationDepth : U8Operand<ModulationDepth> {
  NDS_COMMAND(0xca, "modulation-depth", "Modulation Depth");
  static constexpr std::string_view operandName = "depth";

  void execute(Runtime& rt) const {
    rt.out.modulation(ModulationPerformanceTarget::VibratoDepth, static_cast<double>(raw) / 127.0);
  }
};

struct PortamentoSwitch : U8Operand<PortamentoSwitch> {
  NDS_COMMAND(0xce, "portamento", "Portamento");
  static constexpr std::string_view operandName = "enabled";

  void execute(Runtime& rt) const { rt.out.portamentoEnable(raw != 0); }
};

struct PortamentoTime : U8Operand<PortamentoTime> {
  NDS_COMMAND(0xcf, "portamento-time", "Portamento Time");
  static constexpr std::string_view operandName = "time";

  void execute(Runtime& rt) const { rt.out.portamentoTime(raw); }
};

struct NoteWait : U8Operand<NoteWait> {
  NDS_COMMAND(0xc7, "note-wait", "Note Wait");
  static constexpr std::string_view operandName = "enabled";

  void execute(Runtime& rt) const { rt.state.noteWait = raw != 0; }
};

struct Tempo {
  u16 bpm = 0;

  NDS_COMMAND(0xe1, "tempo", "Tempo");

  static Tempo parse(CommandReader& in) { return Tempo{.bpm = in.le16("bpm")}; }

  void execute(Runtime& rt) const {
    if (bpm != 0) {
      rt.out.tempo(static_cast<u32>(std::round(60000000.0 / bpm)));
    }
  }
};

NDS_IGNORED_COMMAND(0x93, OpenTrack, "open-track", "Open Track", 4);
NDS_IGNORED_COMMAND(0xa0, RandomValueCommand, "random-value", "Cmd with Random Value", 5);
NDS_IGNORED_COMMAND(0xa1, VariableCommand, "variable-command", "Cmd with Variable", 2);
NDS_IGNORED_COMMAND(0xa2, IfCommand, "if", "If", 0);
NDS_IGNORED_COMMAND(0xb0, SetVariable, "set-variable", "Set Variable", 3);
NDS_IGNORED_COMMAND(0xb1, AddVariable, "add-variable", "Add Variable", 3);
NDS_IGNORED_COMMAND(0xb2, SubVariable, "sub-variable", "Sub Variable", 3);
NDS_IGNORED_COMMAND(0xb3, MulVariable, "mul-variable", "Mul Variable", 3);
NDS_IGNORED_COMMAND(0xb4, DivVariable, "div-variable", "Div Variable", 3);
NDS_IGNORED_COMMAND(0xb5, ShiftVariable, "shift-variable", "Shift Variable", 3);
NDS_IGNORED_COMMAND(0xb6, RandVariable, "rand-variable", "Rand Variable", 3);
NDS_IGNORED_COMMAND(0xb8, IfVariableEqual, "if-variable-equal", "If Variable ==", 3);
NDS_IGNORED_COMMAND(0xb9, IfVariableGreaterEqual, "if-variable-greater-equal", "If Variable >=", 3);
NDS_IGNORED_COMMAND(0xba, IfVariableGreater, "if-variable-greater", "If Variable >", 3);
NDS_IGNORED_COMMAND(0xbb, IfVariableLessEqual, "if-variable-less-equal", "If Variable <=", 3);
NDS_IGNORED_COMMAND(0xbc, IfVariableLess, "if-variable-less", "If Variable <", 3);
NDS_IGNORED_COMMAND(0xbd, IfVariableNotEqual, "if-variable-not-equal", "If Variable !=", 3);
NDS_IGNORED_COMMAND(0xc2, MasterVolume, "master-volume", "Master Volume", 1);
NDS_IGNORED_COMMAND(0xc6, Priority, "priority", "Priority", 1);
NDS_IGNORED_COMMAND(0xc8, Tie, "tie", "Tie", 1);
NDS_IGNORED_COMMAND(0xc9, PortamentoControl, "portamento-control", "Portamento Control", 1);
NDS_IGNORED_COMMAND(0xcb, ModulationSpeed, "modulation-speed", "Modulation Speed", 1);
NDS_IGNORED_COMMAND(0xcc, ModulationType, "modulation-type", "Modulation Type", 1);
NDS_IGNORED_COMMAND(0xcd, ModulationRange, "modulation-range", "Modulation Range", 1);
NDS_IGNORED_COMMAND(0xd0, AttackRate, "attack-rate", "Attack Rate", 1);
NDS_IGNORED_COMMAND(0xd1, DecayRate, "decay-rate", "Decay Rate", 1);
NDS_IGNORED_COMMAND(0xd2, SustainLevel, "sustain-level", "Sustain Level", 1);
NDS_IGNORED_COMMAND(0xd3, ReleaseRate, "release-rate", "Release Rate", 1);
NDS_IGNORED_COMMAND(0xd4, LoopStart, "loop-start", "Loop Start", 1);
NDS_IGNORED_COMMAND(0xd6, PrintVariable, "print-variable", "Print Variable", 1);
NDS_IGNORED_COMMAND(0xe0, ModulationDelay, "modulation-delay", "Modulation Delay", 2);
NDS_IGNORED_COMMAND(0xe3, SweepPitch, "sweep-pitch", "Sweep Pitch", 2);
NDS_IGNORED_COMMAND(0xfc, LoopEnd, "loop-end", "Loop End", 0);
NDS_IGNORED_COMMAND(0xfe, AllocateTrack, "allocate-track", "Allocate Track", 2);

struct NoOp {
  std::string bytes;

  NDS_KIND("no-op", "No-op");

  static NoOp parse(CommandReader& in) {
    // Many SSEQ opcodes are currently preserved for UI/parity but have no
    // performance effect. Keep their source operand bytes visible.
    return NoOp{
        .bytes = in.remainingBytes().empty() ? std::string{} : in.rawRemainingBytes("bytes"),
    };
  }
};

struct Terminal : NoOperands<Terminal> {
  NDS_KIND("terminal", "Terminal");

  Effects execute(Runtime& rt) const {
    rt.vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = "NDS SSEQ command stopped playback because it is unsupported or truncated",
    });
    return rt.end();
  }
};

#define NDS_COMMAND_TYPES                                                                                              \
  Note, Rest, Program, OpenTrack, Jump, Call, Return, End, RandomValueCommand, VariableCommand, IfCommand,             \
      SetVariable, AddVariable, SubVariable, MulVariable, DivVariable, ShiftVariable, RandVariable, IfVariableEqual,   \
      IfVariableGreaterEqual, IfVariableGreater, IfVariableLessEqual, IfVariableLess, IfVariableNotEqual, Pan, Volume, \
      MasterVolume, Transpose, PitchBend, PitchBendRange, Priority, NoteWait, Tie, PortamentoControl, ModulationDepth, \
      ModulationSpeed, ModulationType, ModulationRange, PortamentoSwitch, PortamentoTime, AttackRate, DecayRate,       \
      SustainLevel, ReleaseRate, LoopStart, ExpressionLevel, PrintVariable, ModulationDelay, Tempo, SweepPitch,        \
      LoopEnd, AllocateTrack, NoOp, Terminal

using DecodedCommand = DecodedBytecodeCommand;

struct PendingBlock {
  u32 offset = 0;
  bool callTarget = false;
};

template <class Command>
[[nodiscard]] DecodedBytecodeCommand ndsBranch(const SequenceDialect& dialect, ByteReader reader, u32 sequenceOffset,
                                               u32 sequenceEnd, u32 begin, bool returns) {
  auto parsed = parseBytecodeCommand<Command>(dialect, reader, begin, sequenceEnd);
  if (!parsed) {
    return truncatedBytecodeCommand<Terminal>(dialect, reader, begin, sequenceEnd);
  }

  const u32 destination = static_cast<u32>(sequenceOffset + 0x1c + parsed->command.relative);
  if (destination >= sequenceEnd) {
    return terminalBytecodeCommand<Terminal>(dialect, reader, begin, begin + 1);
  }

  auto decoded = std::move(parsed->decoded);
  if (returns) {
    decoded.flow.fallthrough = Address{static_cast<u32>(decoded.range.endOffset())};
  }
  decoded.flow.staticTargets = {Address{destination}};
  return decoded;
}

// Performed SSEQ commands let parse() determine length. Ignored source-driver
// commands keep their operand counts in the command declaration.
[[nodiscard]] DecodedBytecodeCommand decodeCommand(const SequenceDialect& dialect, ByteReader reader,
                                                   u32 sequenceOffset, u32 sequenceEnd, u32 offset) {
#define NDS_EMIT(Type) return recordAutoFallthroughBytecodeCommand<Type, Terminal>(dialect, reader, begin, sequenceEnd)
#define NDS_CASE(Type) \
  case Type::opcode:   \
    NDS_EMIT(Type)
#define NDS_BRANCH(Type) \
  case Type::opcode:     \
    return ndsBranch<Type>(dialect, reader, sequenceOffset, sequenceEnd, begin, false)
#define NDS_CALL(Type) \
  case Type::opcode:   \
    return ndsBranch<Type>(dialect, reader, sequenceOffset, sequenceEnd, begin, true)
#define NDS_IGNORE(Type)                                                                                \
  case Type::opcode:                                                                                    \
    return recordIgnoredBytecodeCommand<Type, Terminal>(dialect, reader, sequenceEnd, begin, begin + 1, \
                                                        Type::operandBytes)
#define NDS_TERMINAL(Op) \
  case Op:               \
    return terminalBytecodeCommand<Terminal>(dialect, reader, begin, begin + 1)
#define NDS_EVENT(Type) \
  case Type::opcode:    \
    return recordAutoBytecodeCommand<Type, Terminal>(dialect, reader, begin, sequenceEnd)

  const u32 begin = offset;
  const u8 status = reader.u8At(offset);

  if (status >= Note::firstOpcode && status <= Note::lastOpcode) {
    NDS_EMIT(Note);
  }

  switch (status) {
    NDS_CASE(Rest);
    NDS_CASE(Program);
    NDS_IGNORE(OpenTrack);
    NDS_BRANCH(Jump);
    NDS_CALL(Call);
    NDS_TERMINAL(0x96);
    NDS_IGNORE(RandomValueCommand);
    NDS_IGNORE(VariableCommand);
    NDS_IGNORE(IfCommand);
    NDS_IGNORE(SetVariable);
    NDS_IGNORE(AddVariable);
    NDS_IGNORE(SubVariable);
    NDS_IGNORE(MulVariable);
    NDS_IGNORE(DivVariable);
    NDS_IGNORE(ShiftVariable);
    NDS_IGNORE(RandVariable);
    NDS_IGNORE(IfVariableEqual);
    NDS_IGNORE(IfVariableGreaterEqual);
    NDS_IGNORE(IfVariableGreater);
    NDS_IGNORE(IfVariableLessEqual);
    NDS_IGNORE(IfVariableLess);
    NDS_IGNORE(IfVariableNotEqual);
    NDS_CASE(Pan);
    NDS_CASE(Volume);
    NDS_IGNORE(MasterVolume);
    NDS_CASE(Transpose);
    NDS_CASE(PitchBend);
    NDS_CASE(PitchBendRange);
    NDS_IGNORE(Priority);
    NDS_CASE(NoteWait);
    NDS_IGNORE(Tie);
    NDS_IGNORE(PortamentoControl);
    NDS_CASE(ModulationDepth);
    NDS_IGNORE(ModulationSpeed);
    NDS_IGNORE(ModulationType);
    NDS_IGNORE(ModulationRange);
    NDS_CASE(PortamentoSwitch);
    NDS_CASE(PortamentoTime);
    NDS_IGNORE(AttackRate);
    NDS_IGNORE(DecayRate);
    NDS_IGNORE(SustainLevel);
    NDS_IGNORE(ReleaseRate);
    NDS_IGNORE(LoopStart);
    NDS_CASE(ExpressionLevel);
    NDS_IGNORE(PrintVariable);
    NDS_IGNORE(ModulationDelay);
    NDS_CASE(Tempo);
    NDS_IGNORE(SweepPitch);
    NDS_IGNORE(LoopEnd);
    NDS_EVENT(Return);
    NDS_IGNORE(AllocateTrack);
    NDS_EVENT(End);
    default:
      return terminalBytecodeCommand<Terminal>(dialect, reader, begin, begin + 1);
  }

#undef NDS_IGNORE
#undef NDS_EVENT
#undef NDS_TERMINAL
#undef NDS_CALL
#undef NDS_BRANCH
#undef NDS_CASE
#undef NDS_EMIT
}

[[nodiscard]] TrackProgram makeTrack(u32 startOffset, u32 trackIndex) {
  return TrackProgram{
      .id = TrackId{trackIndex},
      .sourceTrackNumber = trackIndex,
      .startAddress = Address{startOffset},
  };
}

[[nodiscard]] TrackProgram decodeReachableBlocks(ByteReader reader, const SequenceDialect& dialect, u32 sequenceOffset,
                                                 u32 sequenceEnd, u32 startOffset, u32 trackIndex) {
  TrackProgram track = makeTrack(startOffset, trackIndex);
  TrackProgramBuilder builder{track};
  std::map<u32, DecodedCommand> commandsByOffset;
  std::vector<u32> pendingBlocks{startOffset};

  while (!pendingBlocks.empty()) {
    u32 offset = pendingBlocks.back();
    pendingBlocks.pop_back();
    while (hasBytecodeBytes(reader, offset, 1, sequenceEnd) && !commandsByOffset.contains(offset)) {
      auto decoded = decodeCommand(dialect, reader, sequenceOffset, sequenceEnd, offset);
      for (const Address target : decoded.flow.staticTargets) {
        if (target.value < sequenceEnd && !commandsByOffset.contains(target.value)) {
          pendingBlocks.push_back(target.value);
        }
      }
      const auto next = decoded.flow.fallthrough;
      commandsByOffset.emplace(offset, std::move(decoded));
      if (!next) {
        break;
      }
      offset = next->value;
    }
  }

  for (const auto& [offset, decoded] : commandsByOffset) {
    appendDecodedBytecodeCommand(builder, decoded, offset);
  }
  return track;
}

[[nodiscard]] TrackProgram decodeLegacyMalformedFallthrough(ByteReader reader, const SequenceDialect& dialect,
                                                            u32 sequenceOffset, u32 sequenceEnd, u32 startOffset,
                                                            u32 trackIndex) {
  TrackProgram track = makeTrack(startOffset, trackIndex);
  TrackProgramBuilder builder{track};
  u32 offset = startOffset;
  size_t decodedCommands = 0;
  std::set<u32> visitedControlDestinations;
  std::set<u32> decodedOffsets;
  std::set<u32> callTargetOffsets;
  std::vector<PendingBlock> pendingBlocks{{.offset = startOffset}};
  const CommandKindId callKind = bytecodeHandlerFor<Call>(dialect).kind;
  const CommandKindId endKind = bytecodeHandlerFor<End>(dialect).kind;
  const CommandKindId jumpKind = bytecodeHandlerFor<Jump>(dialect).kind;

  while (!pendingBlocks.empty()) {
    const PendingBlock block = pendingBlocks.back();
    pendingBlocks.pop_back();
    offset = block.offset;

    while (hasBytecodeBytes(reader, offset, 1, sequenceEnd) && decodedCommands++ < kMaxTrackCommands) {
      const u32 begin = offset;
      if (decodedOffsets.contains(begin)) {
        break;
      }
      decodedOffsets.insert(begin);

      auto decoded = decodeCommand(dialect, reader, sequenceOffset, sequenceEnd, offset);

      if (!block.callTarget) {
        const auto overlap = std::ranges::find_if(
            callTargetOffsets, [&](u32 target) { return begin < target && target < decoded.range.endOffset(); });
        if (overlap != callTargetOffsets.end()) {
          // Some malformed FAT entries fall through one byte before a real call
          // target. Stop the fallthrough interpretation before it consumes the
          // overlapping subroutine bytes.
          auto end = recordSizedBytecodeCommand<End>(dialect, reader, begin, begin + 1);
          appendDecodedBytecodeCommand(builder, end, begin);
          break;
        }
      }

      if (decoded.kind == jumpKind && !decoded.flow.staticTargets.empty()) {
        const u32 destination = decoded.flow.staticTargets.front().value;
        if (visitedControlDestinations.contains(destination)) {
          auto end = recordSizedBytecodeCommand<End>(dialect, reader, begin, begin + 1);
          appendDecodedBytecodeCommand(builder, end, begin);
          break;
        }
        visitedControlDestinations.insert(destination);
        auto noOp =
            recordSizedBytecodeCommand<NoOp>(dialect, reader, begin, static_cast<u32>(decoded.range.endOffset()));
        appendDecodedBytecodeCommand(builder, noOp, begin);
        offset = destination;
        continue;
      }

      if (decoded.kind == callKind && !decoded.flow.staticTargets.empty()) {
        const u32 destination = decoded.flow.staticTargets.front().value;
        if (!decodedOffsets.contains(destination) && callTargetOffsets.insert(destination).second) {
          pendingBlocks.push_back(PendingBlock{.offset = destination, .callTarget = true});
        }
      }

      const auto next = decoded.flow.fallthrough;
      appendDecodedBytecodeCommand(builder, decoded, begin);
      if (!next || decoded.flow.terminal || decoded.kind == endKind) {
        break;
      }
      offset = next->value;
    }
  }

  return track;
}

}  // namespace

SequenceDialect ndsSequenceDialect() {
  return SequenceDialectBuilder<TrackState, Context>(kNdsSequenceDialectId, Context{})
      .timebase(Timebase{.ppqn = 0x30})
      .defaultBehavior(SequenceProgramBehavior{
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
          .commandLimit = static_cast<u32>(kMaxTrackCommands),
      })
      .commands<NDS_COMMAND_TYPES>();
}

void registerNdsSequenceDialect(SequenceDialectRegistry& registry) {
  registry.add(ndsSequenceDialect());
}

TrackProgram decodeNdsSequenceTrack(ByteReader reader, const SequenceDialect& dialect, u32 sequenceOffset,
                                    u32 sequenceEnd, u32 startOffset, u32 trackIndex,
                                    bool linearizeMalformedControlFlow) {
  if (linearizeMalformedControlFlow) {
    return decodeLegacyMalformedFallthrough(reader, dialect, sequenceOffset, sequenceEnd, startOffset, trackIndex);
  }
  return decodeReachableBlocks(reader, dialect, sequenceOffset, sequenceEnd, startOffset, trackIndex);
}

std::vector<u32> ndsSequenceTrackStarts(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd) {
  std::vector<u32> extraStarts;
  u32 offset = sequenceOffset + 0x1c;
  if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
    return {};
  }

  if (reader.u8At(offset) == 0xfe) {
    if (!hasBytecodeBytes(reader, offset, 3, sequenceEnd)) {
      return {offset};
    }
    offset += 3;
    if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
      return {offset};
    }
    u8 status = reader.u8At(offset);
    while (status == 0x80) {
      ++offset;
      static_cast<void>(readVarLen(reader, offset, sequenceEnd));
      if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
        return {offset};
      }
      status = reader.u8At(offset);
      break;
    }

    while (status == 0x93 && hasBytecodeBytes(reader, offset, 5, sequenceEnd)) {
      if (const auto start = readSseqAddress(reader, sequenceOffset, sequenceEnd, offset + 2);
          start && *start < sequenceEnd) {
        extraStarts.push_back(*start);
      }
      offset += 5;
      if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
        break;
      }
      status = reader.u8At(offset);
    }
  }

  std::vector<u32> starts{offset};
  starts.insert(starts.end(), extraStarts.begin(), extraStarts.end());
  return starts;
}

#undef NDS_COMMAND_TYPES
#undef NDS_IGNORED_COMMAND
#undef NDS_COMMAND
#undef NDS_KIND

}  // namespace vgmtrans::formats::nds
