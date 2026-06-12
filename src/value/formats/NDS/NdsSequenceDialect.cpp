/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequenceDialect.h"

#include "value/core/SequenceVm.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

#define NDS_COMMAND(Suffix, DisplayName)                  \
  static constexpr std::string_view kind = "nds." Suffix; \
  static constexpr std::string_view name = DisplayName

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

template <class Derived>
struct EmptyParse {
  static Derived parse(CommandReader&) { return {}; }
};

template <class Derived>
struct RawU8 {
  u8 raw = 0;

  static Derived parse(CommandReader& in) {
    Derived result;
    result.raw = in.u8(Derived::operandName);
    return result;
  }
};

template <class Derived>
struct RawS8 {
  s8 raw = 0;

  static Derived parse(CommandReader& in) {
    Derived result;
    result.raw = in.s8(Derived::operandName);
    return result;
  }
};

[[nodiscard]] u32 absoluteAddress(const TrackState& state, u32 relative) {
  return static_cast<u32>(state.sequenceDataBase + relative);
}

[[nodiscard]] bool hasSequenceBytes(ByteReader reader, u32 offset, u32 size, u32 sequenceEnd) {
  return offset <= sequenceEnd && size <= sequenceEnd - offset && reader.has(offset, size);
}

[[nodiscard]] u32 readVarLen(ByteReader reader, u32& offset, u32 sequenceEnd) {
  u32 value = 0;
  while (hasSequenceBytes(reader, offset, 1, sequenceEnd)) {
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
  if (!hasSequenceBytes(reader, operandOffset, 3, sequenceEnd)) {
    return std::nullopt;
  }
  return reader.u8At(operandOffset) + (reader.u8At(operandOffset + 1) << 8) + (reader.u8At(operandOffset + 2) << 16) +
         sequenceOffset + 0x1c;
}

struct Note {
  u8 key = 0;
  u8 velocity = 0;
  u32 duration = 0;

  NDS_COMMAND("note", "Note");

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

  NDS_COMMAND("rest", "Rest");

  static Rest parse(CommandReader& in) { return Rest{.duration = in.varLen("duration")}; }

  Effects execute(Runtime& rt) const { return rt.wait(duration); }
};

struct Program {
  u32 raw = 0;

  NDS_COMMAND("program", "Program");

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
  NDS_COMMAND("jump", "Jump");

  Effects execute(Runtime& rt) const { return rt.jump(Address{absoluteAddress(rt.state, relative)}); }
};

struct Call : RelativeAddressCommand<Call> {
  NDS_COMMAND("call", "Call");

  Effects execute(Runtime& rt) const { return rt.call(Address{absoluteAddress(rt.state, relative)}); }
};

struct Return : EmptyParse<Return> {
  NDS_COMMAND("return", "Return");

  Effects execute(Runtime& rt) const { return rt.return_(); }
};

struct End : EmptyParse<End> {
  NDS_COMMAND("end", "End");

  Effects execute(Runtime& rt) const { return rt.end(); }
};

struct Pan : RawU8<Pan> {
  NDS_COMMAND("pan", "Pan");
  static constexpr std::string_view operandName = "pan";

  void execute(Runtime& rt) const { rt.out.pan(std::clamp((static_cast<double>(raw) / 63.5) - 1.0, -1.0, 1.0)); }
};

struct Volume : RawU8<Volume> {
  NDS_COMMAND("volume", "Volume");
  static constexpr std::string_view operandName = "volume";

  void execute(Runtime& rt) const { rt.out.level(std::clamp(static_cast<double>(raw) / 127.0, 0.0, 1.0)); }
};

struct ExpressionLevel : RawU8<ExpressionLevel> {
  NDS_COMMAND("expression", "Expression");
  static constexpr std::string_view operandName = "expression";

  void execute(Runtime& rt) const { rt.out.expression(std::clamp(static_cast<double>(raw) / 127.0, 0.0, 1.0)); }
};

struct Transpose {
  s8 semitones = 0;

  NDS_COMMAND("transpose", "Transpose");

  static Transpose parse(CommandReader& in) { return Transpose{.semitones = in.s8("semitones")}; }

  void execute(Runtime& rt) const { rt.state.transpose = semitones; }
};

struct PitchBend : RawS8<PitchBend> {
  NDS_COMMAND("pitch-bend", "Pitch Bend");
  static constexpr std::string_view operandName = "bend";

  void execute(Runtime& rt) const { rt.out.pitchBend(static_cast<s16>(raw * 64)); }
};

struct PitchBendRange : RawU8<PitchBendRange> {
  NDS_COMMAND("pitch-bend-range", "Pitch Bend Range");
  static constexpr std::string_view operandName = "semitones";

  void execute(Runtime& rt) const { rt.out.pitchBendRange(raw); }
};

struct ModulationDepth : RawU8<ModulationDepth> {
  NDS_COMMAND("modulation-depth", "Modulation Depth");
  static constexpr std::string_view operandName = "depth";

  void execute(Runtime& rt) const {
    rt.out.modulation(ModulationPerformanceTarget::VibratoDepth, static_cast<double>(raw) / 127.0);
  }
};

struct PortamentoSwitch : RawU8<PortamentoSwitch> {
  NDS_COMMAND("portamento", "Portamento");
  static constexpr std::string_view operandName = "enabled";

  void execute(Runtime& rt) const { rt.out.portamentoEnable(raw != 0); }
};

struct PortamentoTime : RawU8<PortamentoTime> {
  NDS_COMMAND("portamento-time", "Portamento Time");
  static constexpr std::string_view operandName = "time";

  void execute(Runtime& rt) const { rt.out.portamentoTime(raw); }
};

struct NoteWait : RawU8<NoteWait> {
  NDS_COMMAND("note-wait", "Note Wait");
  static constexpr std::string_view operandName = "enabled";

  void execute(Runtime& rt) const { rt.state.noteWait = raw != 0; }
};

struct Tempo {
  u16 bpm = 0;

  NDS_COMMAND("tempo", "Tempo");

  static Tempo parse(CommandReader& in) { return Tempo{.bpm = in.le16("bpm")}; }

  void execute(Runtime& rt) const {
    if (bpm != 0) {
      rt.out.tempo(static_cast<u32>(std::round(60000000.0 / bpm)));
    }
  }
};

struct OneByteNoOp : RawU8<OneByteNoOp> {
  NDS_COMMAND("no-op-1", "No-op");
  static constexpr std::string_view operandName = "value";
};

struct NoOp {
  std::string bytes;

  NDS_COMMAND("no-op", "No-op");

  static NoOp parse(CommandReader& in) {
    // Many SSEQ opcodes are currently preserved for UI/parity but have no
    // performance effect. Keep their source operand bytes visible.
    return NoOp{
        .bytes = in.remainingBytes().empty() ? std::string{} : in.rawRemainingBytes("bytes"),
    };
  }
};

struct Terminal : EmptyParse<Terminal> {
  NDS_COMMAND("terminal", "Terminal");

  Effects execute(Runtime& rt) const {
    rt.vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = "NDS SSEQ command stopped playback because it is unsupported or truncated",
    });
    return rt.end();
  }
};

struct DecodedCommand {
  CommandHandlerId handler;
  CommandKindId kind;
  SourceRange range;
  std::vector<u8> bytes;
  std::vector<CommandOperand> operands;
  DecodeFlow flow;
};

struct PendingBlock {
  u32 offset = 0;
  bool callTarget = false;
};

[[nodiscard]] const CommandHandler& handlerFor(const SequenceDialect& dialect, std::string_view kind) {
  const auto* handler = dialect.handlerForKind(kind);
  if (handler == nullptr) {
    throw std::logic_error("NDS SSEQ command was not registered in its dialect");
  }
  return *handler;
}

template <class Command>
[[nodiscard]] DecodedCommand recordCommand(const SequenceDialect& dialect, ByteReader reader, u32 begin, u32 end) {
  const auto& handler = handlerFor(dialect, Command::kind);
  const SourceRange range = reader.range(begin, end - begin);
  const auto bytes = reader.slice(begin, end - begin);
  std::vector<u8> ownedBytes{bytes.begin(), bytes.end()};
  std::vector<CommandOperand> operands;
  CommandReader commandReader{range, ownedBytes, &operands};
  static_cast<void>(Command::parse(commandReader));
  if (!commandReader.done()) {
    throw std::invalid_argument("NDS SSEQ command parser left trailing source bytes");
  }
  return DecodedCommand{
      .handler = handler.id,
      .kind = handler.kind,
      .range = range,
      .bytes = std::move(ownedBytes),
      .operands = std::move(operands),
  };
}

template <class Command>
[[nodiscard]] DecodedCommand recordFallthrough(const SequenceDialect& dialect, ByteReader reader, u32 begin, u32 end) {
  auto decoded = recordCommand<Command>(dialect, reader, begin, end);
  decoded.flow.fallthrough = Address{end};
  return decoded;
}

[[nodiscard]] DecodedCommand terminalCommand(const SequenceDialect& dialect, ByteReader reader, u32 begin, u32 end) {
  auto decoded = recordCommand<Terminal>(dialect, reader, begin, end);
  decoded.flow.terminal = true;
  return decoded;
}

[[nodiscard]] DecodedCommand recordIgnored(const SequenceDialect& dialect, ByteReader reader, u32 sequenceEnd,
                                           u32 begin, u32 operandOffset, u32 operandBytes) {
  if (!hasSequenceBytes(reader, operandOffset, operandBytes, sequenceEnd)) {
    return terminalCommand(dialect, reader, begin, operandOffset);
  }
  return recordFallthrough<NoOp>(dialect, reader, begin, operandOffset + operandBytes);
}

template <class Command>
[[nodiscard]] DecodedCommand recordFixed(const SequenceDialect& dialect, ByteReader reader, u32 sequenceEnd, u32 begin,
                                         u32 operandOffset, u32 operandBytes) {
  if (!hasSequenceBytes(reader, operandOffset, operandBytes, sequenceEnd)) {
    return terminalCommand(dialect, reader, begin, operandOffset);
  }
  return recordFallthrough<Command>(dialect, reader, begin, operandOffset + operandBytes);
}

template <class Command>
[[nodiscard]] DecodedCommand recordVarLen(const SequenceDialect& dialect, ByteReader reader, u32 sequenceEnd, u32 begin,
                                          u32& offset) {
  static_cast<void>(readVarLen(reader, offset, sequenceEnd));
  return recordFallthrough<Command>(dialect, reader, begin, offset);
}

template <class Command>
[[nodiscard]] DecodedCommand recordBranch(const SequenceDialect& dialect, ByteReader reader, u32 sequenceOffset,
                                          u32 sequenceEnd, u32 begin, u32 operandOffset, bool returns) {
  const auto destination = readSseqAddress(reader, sequenceOffset, sequenceEnd, operandOffset);
  if (!destination || *destination >= sequenceEnd) {
    return terminalCommand(dialect, reader, begin, operandOffset);
  }

  const u32 end = operandOffset + 3;
  auto decoded = recordCommand<Command>(dialect, reader, begin, end);
  if (returns) {
    decoded.flow.fallthrough = Address{end};
  }
  decoded.flow.staticTargets = {Address{*destination}};
  return decoded;
}

[[nodiscard]] bool isUnhandledOneByteCommand(u8 status) {
  switch (status) {
    case 0xc3:
    case 0xc4:
    case 0xc5:
    case 0xc7:
    case 0xca:
    case 0xce:
    case 0xcf:
    case 0xd5:
      return false;
    default:
      return status >= 0xc2 && status <= 0xd6;
  }
}

[[nodiscard]] DecodedCommand decodeCommand(const SequenceDialect& dialect, ByteReader reader, u32 sequenceOffset,
                                           u32 sequenceEnd, u32 offset) {
  const u32 begin = offset;
  const u8 status = reader.u8At(offset++);

  if (status < 0x80) {
    if (!hasSequenceBytes(reader, offset, 1, sequenceEnd)) {
      return terminalCommand(dialect, reader, begin, offset);
    }
    ++offset;
    return recordVarLen<Note>(dialect, reader, sequenceEnd, begin, offset);
  }

  switch (status) {
    case 0x80:
      return recordVarLen<Rest>(dialect, reader, sequenceEnd, begin, offset);
    case 0x81:
      return recordVarLen<Program>(dialect, reader, sequenceEnd, begin, offset);
    case 0x93:
      return recordIgnored(dialect, reader, sequenceEnd, begin, offset, 4);
    case 0x94:
      return recordBranch<Jump>(dialect, reader, sequenceOffset, sequenceEnd, begin, offset, false);
    case 0x95:
      return recordBranch<Call>(dialect, reader, sequenceOffset, sequenceEnd, begin, offset, true);
    case 0x96:
      return terminalCommand(dialect, reader, begin, offset);
    case 0xa0:
      return recordIgnored(dialect, reader, sequenceEnd, begin, offset, 5);
    case 0xa1:
      return recordIgnored(dialect, reader, sequenceEnd, begin, offset, 2);
    case 0xa2:
      return recordIgnored(dialect, reader, sequenceEnd, begin, offset, 0);
    case 0xb0:
    case 0xb1:
    case 0xb2:
    case 0xb3:
    case 0xb4:
    case 0xb5:
    case 0xb6:
    case 0xb8:
    case 0xb9:
    case 0xba:
    case 0xbb:
    case 0xbc:
    case 0xbd:
      return recordIgnored(dialect, reader, sequenceEnd, begin, offset, 3);
    case 0xc0:
      return recordFixed<Pan>(dialect, reader, sequenceEnd, begin, offset, 1);
    case 0xc1:
      return recordFixed<Volume>(dialect, reader, sequenceEnd, begin, offset, 1);
    case 0xc3:
      return recordFixed<Transpose>(dialect, reader, sequenceEnd, begin, offset, 1);
    case 0xc4:
      return recordFixed<PitchBend>(dialect, reader, sequenceEnd, begin, offset, 1);
    case 0xc5:
      return recordFixed<PitchBendRange>(dialect, reader, sequenceEnd, begin, offset, 1);
    case 0xc7:
      return recordFixed<NoteWait>(dialect, reader, sequenceEnd, begin, offset, 1);
    case 0xca:
      return recordFixed<ModulationDepth>(dialect, reader, sequenceEnd, begin, offset, 1);
    case 0xce:
      return recordFixed<PortamentoSwitch>(dialect, reader, sequenceEnd, begin, offset, 1);
    case 0xcf:
      return recordFixed<PortamentoTime>(dialect, reader, sequenceEnd, begin, offset, 1);
    case 0xd5:
      return recordFixed<ExpressionLevel>(dialect, reader, sequenceEnd, begin, offset, 1);
    case 0xe1:
      return recordFixed<Tempo>(dialect, reader, sequenceEnd, begin, offset, 2);
    case 0xe0:
    case 0xe3:
      return recordIgnored(dialect, reader, sequenceEnd, begin, offset, 2);
    case 0xfd:
      return recordCommand<Return>(dialect, reader, begin, offset);
    case 0xff:
      return recordCommand<End>(dialect, reader, begin, offset);
    case 0xfc:
      return recordIgnored(dialect, reader, sequenceEnd, begin, offset, 0);
    case 0xfe:
      return recordIgnored(dialect, reader, sequenceEnd, begin, offset, 2);
    default:
      if (isUnhandledOneByteCommand(status)) {
        return recordFixed<OneByteNoOp>(dialect, reader, sequenceEnd, begin, offset, 1);
      }
      return terminalCommand(dialect, reader, begin, offset);
  }
}

void appendDecoded(TrackProgramBuilder& builder, const DecodedCommand& decoded, u32 offset) {
  builder.addDecoded(decoded.handler, decoded.kind, Address{offset}, decoded.range, decoded.bytes, decoded.operands);
}

}  // namespace

SequenceDialect ndsSequenceDialect() {
  return SequenceDialectBuilder<TrackState, Context>(kNdsSequenceDialectId, Context{})
      .timebase(Timebase{.ppqn = 0x30})
      .defaultBehavior(SequenceProgramBehavior{
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
          .commandLimit = static_cast<u32>(kMaxTrackCommands),
      })
      .commands<Note, Rest, Program, Jump, Call, Return, End, Pan, Volume, Transpose, NoteWait, Tempo, OneByteNoOp,
                ExpressionLevel, PitchBend, PitchBendRange, ModulationDepth, PortamentoSwitch, PortamentoTime, NoOp,
                Terminal>();
}

void registerNdsSequenceDialect(SequenceDialectRegistry& registry) {
  registry.add(ndsSequenceDialect());
}

TrackProgram decodeNdsSequenceTrack(ByteReader reader, const SequenceDialect& dialect, u32 sequenceOffset,
                                    u32 sequenceEnd, u32 startOffset, u32 trackIndex,
                                    bool linearizeMalformedControlFlow) {
  TrackProgram track{
      .id = TrackId{trackIndex},
      .sourceTrackNumber = trackIndex,
      .startAddress = Address{startOffset},
  };
  TrackProgramBuilder builder{track};

  if (!linearizeMalformedControlFlow) {
    std::map<u32, DecodedCommand> commandsByOffset;
    std::vector<u32> pendingBlocks{startOffset};

    while (!pendingBlocks.empty()) {
      u32 offset = pendingBlocks.back();
      pendingBlocks.pop_back();
      while (hasSequenceBytes(reader, offset, 1, sequenceEnd) && !commandsByOffset.contains(offset)) {
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
      appendDecoded(builder, decoded, offset);
    }
    return track;
  }

  u32 offset = startOffset;
  size_t decodedCommands = 0;
  std::set<u32> visitedControlDestinations;
  std::set<u32> decodedOffsets;
  std::set<u32> callTargetOffsets;
  std::vector<PendingBlock> pendingBlocks{{.offset = startOffset}};
  const CommandKindId callKind = handlerFor(dialect, Call::kind).kind;
  const CommandKindId endKind = handlerFor(dialect, End::kind).kind;
  const CommandKindId jumpKind = handlerFor(dialect, Jump::kind).kind;

  while (!pendingBlocks.empty()) {
    const PendingBlock block = pendingBlocks.back();
    pendingBlocks.pop_back();
    offset = block.offset;

    while (hasSequenceBytes(reader, offset, 1, sequenceEnd) && decodedCommands++ < kMaxTrackCommands) {
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
          auto end = recordCommand<End>(dialect, reader, begin, begin + 1);
          appendDecoded(builder, end, begin);
          break;
        }
      }

      if (decoded.kind == jumpKind && !decoded.flow.staticTargets.empty()) {
        const u32 destination = decoded.flow.staticTargets.front().value;
        if (visitedControlDestinations.contains(destination)) {
          auto end = recordCommand<End>(dialect, reader, begin, begin + 1);
          appendDecoded(builder, end, begin);
          break;
        }
        visitedControlDestinations.insert(destination);
        auto noOp = recordCommand<NoOp>(dialect, reader, begin, static_cast<u32>(decoded.range.endOffset()));
        appendDecoded(builder, noOp, begin);
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
      appendDecoded(builder, decoded, begin);
      if (!next || decoded.flow.terminal || decoded.kind == endKind) {
        break;
      }
      offset = next->value;
    }
  }

  return track;
}

std::vector<u32> ndsSequenceTrackStarts(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd) {
  std::vector<u32> extraStarts;
  u32 offset = sequenceOffset + 0x1c;
  if (!hasSequenceBytes(reader, offset, 1, sequenceEnd)) {
    return {};
  }

  if (reader.u8At(offset) == 0xfe) {
    if (!hasSequenceBytes(reader, offset, 3, sequenceEnd)) {
      return {offset};
    }
    offset += 3;
    if (!hasSequenceBytes(reader, offset, 1, sequenceEnd)) {
      return {offset};
    }
    u8 status = reader.u8At(offset);
    while (status == 0x80) {
      ++offset;
      static_cast<void>(readVarLen(reader, offset, sequenceEnd));
      if (!hasSequenceBytes(reader, offset, 1, sequenceEnd)) {
        return {offset};
      }
      status = reader.u8At(offset);
      break;
    }

    while (status == 0x93 && hasSequenceBytes(reader, offset, 5, sequenceEnd)) {
      if (const auto start = readSseqAddress(reader, sequenceOffset, sequenceEnd, offset + 2);
          start && *start < sequenceEnd) {
        extraStarts.push_back(*start);
      }
      offset += 5;
      if (!hasSequenceBytes(reader, offset, 1, sequenceEnd)) {
        break;
      }
      status = reader.u8At(offset);
    }
  }

  std::vector<u32> starts{offset};
  starts.insert(starts.end(), extraStarts.begin(), extraStarts.end());
  return starts;
}

#undef NDS_COMMAND

}  // namespace vgmtrans::formats::nds
