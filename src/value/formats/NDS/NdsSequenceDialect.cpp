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

// Keep these macros to one-byte commands whose behavior fits on one line.
#define NDS_U8_NORMALIZED_OUT(Type, Op, Suffix, DisplayName, Operand, Method)                                  \
  struct Type : U8Operand<Type> {                                                                              \
    NDS_COMMAND(Op, Suffix, DisplayName);                                                                      \
    static constexpr std::string_view operandName = Operand;                                                   \
    void execute(Runtime& rt) const { rt.out.Method(std::clamp(static_cast<double>(raw) / 127.0, 0.0, 1.0)); } \
  }

#define NDS_U8_RAW_OUT(Type, Op, Suffix, DisplayName, Operand, Method) \
  struct Type : U8Operand<Type> {                                      \
    NDS_COMMAND(Op, Suffix, DisplayName);                              \
    static constexpr std::string_view operandName = Operand;           \
    void execute(Runtime& rt) const { rt.out.Method(raw); }            \
  }

#define NDS_U8_BOOL_OUT(Type, Op, Suffix, DisplayName, Operand, Method) \
  struct Type : U8Operand<Type> {                                       \
    NDS_COMMAND(Op, Suffix, DisplayName);                               \
    static constexpr std::string_view operandName = Operand;            \
    void execute(Runtime& rt) const { rt.out.Method(raw != 0); }        \
  }

#define NDS_U8_BOOL_STATE(Type, Op, Suffix, DisplayName, Operand, Member) \
  struct Type : U8Operand<Type> {                                         \
    NDS_COMMAND(Op, Suffix, DisplayName);                                 \
    static constexpr std::string_view operandName = Operand;              \
    void execute(Runtime& rt) const { rt.state.Member = raw != 0; }       \
  }

#define NDS_U8_MODULATION(Type, Op, Suffix, DisplayName, Operand, Target)                            \
  struct Type : U8Operand<Type> {                                                                    \
    NDS_COMMAND(Op, Suffix, DisplayName);                                                            \
    static constexpr std::string_view operandName = Operand;                                         \
    void execute(Runtime& rt) const { rt.out.modulation(Target, static_cast<double>(raw) / 127.0); } \
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

  static Program parse(CommandReader& in) { return Program{.raw = in.varLen("raw")}; }

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

NDS_U8_NORMALIZED_OUT(Volume, 0xc1, "volume", "Volume", "volume", level);
NDS_U8_NORMALIZED_OUT(ExpressionLevel, 0xd5, "expression", "Expression", "expression", expression);

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

NDS_U8_RAW_OUT(PitchBendRange, 0xc5, "pitch-bend-range", "Pitch Bend Range", "semitones", pitchBendRange);
NDS_U8_MODULATION(ModulationDepth, 0xca, "modulation-depth", "Modulation Depth", "depth",
                  ModulationPerformanceTarget::VibratoDepth);
NDS_U8_BOOL_OUT(PortamentoSwitch, 0xce, "portamento", "Portamento", "enabled", portamentoEnable);
NDS_U8_RAW_OUT(PortamentoTime, 0xcf, "portamento-time", "Portamento Time", "time", portamentoTime);
NDS_U8_BOOL_STATE(NoteWait, 0xc7, "note-wait", "Note Wait", "enabled", noteWait);

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

#define NDS_PRESERVED(Op, Suffix, DisplayName, OperandBytes) \
  PreservedBytecodeCommandSpec {                             \
    Op, "nds." Suffix, DisplayName, OperandBytes             \
  }

constexpr PreservedBytecodeCommandSpec kNdsPreservedCommands[] = {
    NDS_PRESERVED(0x93, "open-track", "Open Track", 4),
    NDS_PRESERVED(0xa0, "random-value", "Cmd with Random Value", 5),
    NDS_PRESERVED(0xa1, "variable-command", "Cmd with Variable", 2),
    NDS_PRESERVED(0xa2, "if", "If", 0),
    NDS_PRESERVED(0xb0, "set-variable", "Set Variable", 3),
    NDS_PRESERVED(0xb1, "add-variable", "Add Variable", 3),
    NDS_PRESERVED(0xb2, "sub-variable", "Sub Variable", 3),
    NDS_PRESERVED(0xb3, "mul-variable", "Mul Variable", 3),
    NDS_PRESERVED(0xb4, "div-variable", "Div Variable", 3),
    NDS_PRESERVED(0xb5, "shift-variable", "Shift Variable", 3),
    NDS_PRESERVED(0xb6, "rand-variable", "Rand Variable", 3),
    NDS_PRESERVED(0xb8, "if-variable-equal", "If Variable ==", 3),
    NDS_PRESERVED(0xb9, "if-variable-greater-equal", "If Variable >=", 3),
    NDS_PRESERVED(0xba, "if-variable-greater", "If Variable >", 3),
    NDS_PRESERVED(0xbb, "if-variable-less-equal", "If Variable <=", 3),
    NDS_PRESERVED(0xbc, "if-variable-less", "If Variable <", 3),
    NDS_PRESERVED(0xbd, "if-variable-not-equal", "If Variable !=", 3),
    NDS_PRESERVED(0xc2, "master-volume", "Master Volume", 1),
    NDS_PRESERVED(0xc6, "priority", "Priority", 1),
    NDS_PRESERVED(0xc8, "tie", "Tie", 1),
    NDS_PRESERVED(0xc9, "portamento-control", "Portamento Control", 1),
    NDS_PRESERVED(0xcb, "modulation-speed", "Modulation Speed", 1),
    NDS_PRESERVED(0xcc, "modulation-type", "Modulation Type", 1),
    NDS_PRESERVED(0xcd, "modulation-range", "Modulation Range", 1),
    NDS_PRESERVED(0xd0, "attack-rate", "Attack Rate", 1),
    NDS_PRESERVED(0xd1, "decay-rate", "Decay Rate", 1),
    NDS_PRESERVED(0xd2, "sustain-level", "Sustain Level", 1),
    NDS_PRESERVED(0xd3, "release-rate", "Release Rate", 1),
    NDS_PRESERVED(0xd4, "loop-start", "Loop Start", 1),
    NDS_PRESERVED(0xd6, "print-variable", "Print Variable", 1),
    NDS_PRESERVED(0xe0, "modulation-delay", "Modulation Delay", 2),
    NDS_PRESERVED(0xe3, "sweep-pitch", "Sweep Pitch", 2),
    NDS_PRESERVED(0xfc, "loop-end", "Loop End", 0),
    NDS_PRESERVED(0xfe, "allocate-track", "Allocate Track", 2),
};

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

#define NDS_FALLTHROUGH_COMMANDS(X) \
  X(Rest)                           \
  X(Program)                        \
  X(Pan)                            \
  X(Volume)                         \
  X(Transpose)                      \
  X(PitchBend)                      \
  X(PitchBendRange)                 \
  X(NoteWait)                       \
  X(ModulationDepth)                \
  X(PortamentoSwitch)               \
  X(PortamentoTime)                 \
  X(ExpressionLevel)                \
  X(Tempo)

#define NDS_TYPE(Type) Type,
#define NDS_COMMAND_TYPES Note, NDS_FALLTHROUGH_COMMANDS(NDS_TYPE) Jump, Call, Return, End, NoOp, Terminal

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

[[nodiscard]] const PreservedBytecodeCommandSpec* preservedCommand(u8 opcode) {
  const auto found =
      std::ranges::find_if(kNdsPreservedCommands, [opcode](const auto& command) { return command.opcode == opcode; });
  return found == std::end(kNdsPreservedCommands) ? nullptr : &*found;
}

// Performed SSEQ commands let parse() determine length. Preserved source-driver
// commands keep fixed operand counts in kNdsPreservedCommands.
[[nodiscard]] DecodedBytecodeCommand decodeCommand(const SequenceDialect& dialect, ByteReader reader,
                                                   u32 sequenceOffset, u32 sequenceEnd, u32 offset) {
#define NDS_EMIT(Type) return recordAutoFallthroughBytecodeCommand<Type, Terminal>(dialect, reader, begin, sequenceEnd);
#define NDS_CASE(Type) \
  case Type::opcode:   \
    NDS_EMIT(Type)
#define NDS_BRANCH(Type) \
  case Type::opcode:     \
    return ndsBranch<Type>(dialect, reader, sequenceOffset, sequenceEnd, begin, false)
#define NDS_CALL(Type) \
  case Type::opcode:   \
    return ndsBranch<Type>(dialect, reader, sequenceOffset, sequenceEnd, begin, true)
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
  if (const auto* preserved = preservedCommand(status); preserved != nullptr) {
    return recordPreservedBytecodeCommand<Terminal>(dialect, reader, sequenceEnd, begin, begin + 1, *preserved);
  }

  switch (status) {
    NDS_FALLTHROUGH_COMMANDS(NDS_CASE)
    NDS_BRANCH(Jump);
    NDS_CALL(Call);
    NDS_TERMINAL(0x96);
    NDS_EVENT(Return);
    NDS_EVENT(End);
    default:
      return terminalBytecodeCommand<Terminal>(dialect, reader, begin, begin + 1);
  }

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
  return decodeReachableBytecodeBlocks(
      reader, sequenceEnd, startOffset, trackIndex,
      ReachableBytecodeDecodePolicy{.maxCommands = static_cast<u32>(kMaxTrackCommands)},
      [&](u32 offset) { return decodeCommand(dialect, reader, sequenceOffset, sequenceEnd, offset); });
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
      .preservedCommands(std::span<const PreservedBytecodeCommandSpec>{kNdsPreservedCommands})
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
#undef NDS_TYPE
#undef NDS_FALLTHROUGH_COMMANDS
#undef NDS_PRESERVED
#undef NDS_U8_MODULATION
#undef NDS_U8_BOOL_STATE
#undef NDS_U8_BOOL_OUT
#undef NDS_U8_RAW_OUT
#undef NDS_U8_NORMALIZED_OUT
#undef NDS_COMMAND
#undef NDS_KIND

}  // namespace vgmtrans::formats::nds
