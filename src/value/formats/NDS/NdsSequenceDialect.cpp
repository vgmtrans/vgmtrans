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

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr size_t kMaxTrackCommands = 262144;

struct Context {};

struct TrackState {
  explicit TrackState(const SequenceProgram& program, const TrackProgram&) : sequenceDataBase(program.sourceBaseAddress.value) {}

  u64 sequenceDataBase = 0;
  bool noteWait = false;
  s32 transpose = 0;
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

  static constexpr std::string_view kind = "nds.note";
  static constexpr std::string_view name = "Note";

  static Note parse(CommandReader& in) {
    return Note{
        .key = in.opcode(),
        .velocity = in.u8("velocity"),
        .duration = in.varLen("duration"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("key", static_cast<u64>(key));
    out.field("velocity", static_cast<u64>(velocity));
    out.field("duration", static_cast<u64>(duration));
  }

  Effects execute(TrackState& state, Emit& out, VmApi&, const Context&) const {
    out.note(NotePerformanceEvent{
        .key = static_cast<double>(std::clamp<s32>(static_cast<s32>(key) + state.transpose, 0, 127)),
        .velocity = velocity / 127.0,
        .durationTicks = duration,
    });
    return Effects::wait(state.noteWait ? duration : 0);
  }
};

struct Rest {
  u32 duration = 0;

  static constexpr std::string_view kind = "nds.rest";
  static constexpr std::string_view name = "Rest";

  static Rest parse(CommandReader& in) {
    return Rest{.duration = in.varLen("duration")};
  }

  void describe(CommandInfo& out) const {
    out.field("duration", static_cast<u64>(duration));
  }

  Effects execute(TrackState&, Emit&, VmApi&, const Context&) const {
    return Effects::wait(duration);
  }
};

struct Program {
  u32 raw = 0;

  static constexpr std::string_view kind = "nds.program";
  static constexpr std::string_view name = "Program";

  static Program parse(CommandReader& in) {
    return Program{.raw = in.varLen("program")};
  }

  void describe(CommandInfo& out) const {
    out.field("bank", static_cast<u64>(raw >> 7));
    out.field("program", static_cast<u64>(raw & 0x7f));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context&) const {
    out.instrument(InstrumentPerformanceEvent{
        .bank = raw >> 7,
        .program = raw & 0x7f,
    });
    return Effects::none();
  }
};

struct RelativeAddressCommand {
  u32 relative = 0;

  static RelativeAddressCommand parse(CommandReader& in) {
    return RelativeAddressCommand{.relative = in.le24("destination")};
  }
};

struct Jump : RelativeAddressCommand {
  static constexpr std::string_view kind = "nds.jump";
  static constexpr std::string_view name = "Jump";

  static Jump parse(CommandReader& in) {
    return Jump{RelativeAddressCommand::parse(in)};
  }

  void describe(CommandInfo& out) const {
    out.field("destination", static_cast<u64>(relative));
  }

  Effects execute(TrackState& state, Emit&, VmApi& vm, const Context&) const {
    return Effects{.step = vm.jump(Address{absoluteAddress(state, relative)})};
  }
};

struct Call : RelativeAddressCommand {
  static constexpr std::string_view kind = "nds.call";
  static constexpr std::string_view name = "Call";

  static Call parse(CommandReader& in) {
    return Call{RelativeAddressCommand::parse(in)};
  }

  void describe(CommandInfo& out) const {
    out.field("destination", static_cast<u64>(relative));
  }

  Effects execute(TrackState& state, Emit&, VmApi& vm, const Context&) const {
    return Effects{.step = vm.call(Address{absoluteAddress(state, relative)})};
  }
};

struct Return {
  static constexpr std::string_view kind = "nds.return";
  static constexpr std::string_view name = "Return";

  static Return parse(CommandReader&) {
    return Return{};
  }

  Effects execute(TrackState&, Emit&, VmApi& vm, const Context&) const {
    return Effects{.step = vm.return_()};
  }
};

struct End {
  static constexpr std::string_view kind = "nds.end";
  static constexpr std::string_view name = "End";

  static End parse(CommandReader&) {
    return End{};
  }

  Effects execute(TrackState&, Emit&, VmApi& vm, const Context&) const {
    return Effects{.step = vm.end()};
  }
};

struct Pan {
  u8 raw = 0;

  static constexpr std::string_view kind = "nds.pan";
  static constexpr std::string_view name = "Pan";

  static Pan parse(CommandReader& in) {
    return Pan{.raw = in.u8("pan")};
  }

  void describe(CommandInfo& out) const {
    out.field("raw", static_cast<u64>(raw));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context&) const {
    out.pan(PanPerformanceEvent{
        .stereoPosition = std::clamp((static_cast<double>(raw) / 63.5) - 1.0, -1.0, 1.0),
    });
    return Effects::none();
  }
};

struct Volume {
  u8 raw = 0;

  static constexpr std::string_view kind = "nds.volume";
  static constexpr std::string_view name = "Volume";

  static Volume parse(CommandReader& in) {
    return Volume{.raw = in.u8("volume")};
  }

  void describe(CommandInfo& out) const {
    out.field("raw", static_cast<u64>(raw));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context&) const {
    out.level(LevelPerformanceEvent{
        .linearGain = std::clamp(static_cast<double>(raw) / 127.0, 0.0, 1.0),
    });
    return Effects::none();
  }
};

struct Transpose {
  s8 semitones = 0;

  static constexpr std::string_view kind = "nds.transpose";
  static constexpr std::string_view name = "Transpose";

  static Transpose parse(CommandReader& in) {
    return Transpose{.semitones = in.s8("semitones")};
  }

  void describe(CommandInfo& out) const {
    out.field("semitones", static_cast<s64>(semitones));
  }

  Effects execute(TrackState& state, Emit&, VmApi&, const Context&) const {
    state.transpose = semitones;
    return Effects::none();
  }
};

struct NoteWait {
  u8 raw = 0;

  static constexpr std::string_view kind = "nds.note-wait";
  static constexpr std::string_view name = "Note Wait";

  static NoteWait parse(CommandReader& in) {
    return NoteWait{.raw = in.u8("enabled")};
  }

  void describe(CommandInfo& out) const {
    out.field("enabled", static_cast<u64>(raw));
  }

  Effects execute(TrackState& state, Emit&, VmApi&, const Context&) const {
    state.noteWait = raw != 0;
    return Effects::none();
  }
};

struct Tempo {
  u16 bpm = 0;

  static constexpr std::string_view kind = "nds.tempo";
  static constexpr std::string_view name = "Tempo";

  static Tempo parse(CommandReader& in) {
    return Tempo{.bpm = in.le16("bpm")};
  }

  void describe(CommandInfo& out) const {
    out.field("bpm", static_cast<u64>(bpm));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context&) const {
    if (bpm != 0) {
      out.tempo(TempoPerformanceEvent{
          .microsecondsPerQuarter = static_cast<u32>(std::round(60000000.0 / bpm)),
      });
    }
    return Effects::none();
  }
};

struct OneByteNoOp {
  u8 value = 0;

  static constexpr std::string_view kind = "nds.no-op-1";
  static constexpr std::string_view name = "No-op";

  static OneByteNoOp parse(CommandReader& in) {
    return OneByteNoOp{.value = in.u8("value")};
  }

  void describe(CommandInfo& out) const {
    out.field("value", static_cast<u64>(value));
  }

  Effects execute(TrackState&, Emit&, VmApi&, const Context&) const {
    return Effects::none();
  }
};

struct NoOp {
  std::string bytes;

  static constexpr std::string_view kind = "nds.no-op";
  static constexpr std::string_view name = "No-op";

  static NoOp parse(CommandReader& in) {
    // Many SSEQ opcodes are currently preserved for UI/parity but have no
    // performance effect. Keep their source operand bytes visible.
    return NoOp{
        .bytes = in.remainingBytes().empty() ? std::string{} : in.rawRemainingBytes("bytes"),
    };
  }

  void describe(CommandInfo& out) const {
    if (!bytes.empty()) {
      out.field("bytes", bytes);
    }
  }

  Effects execute(TrackState&, Emit&, VmApi&, const Context&) const {
    return Effects::none();
  }
};

struct Terminal {
  static constexpr std::string_view kind = "nds.terminal";
  static constexpr std::string_view name = "Terminal";

  static Terminal parse(CommandReader&) {
    return Terminal{};
  }

  Effects execute(TrackState&, Emit&, VmApi& vm, const Context&) const {
    vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = "NDS SSEQ command stopped playback because it is unsupported or truncated",
    });
    return Effects{.step = vm.end()};
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

[[nodiscard]] DecodedCommand decodeCommand(const SequenceDialect& dialect, ByteReader reader, u32 sequenceOffset,
                                           u32 sequenceEnd, u32 offset) {
  const u32 begin = offset;
  const u8 status = reader.u8At(offset++);
  auto terminal = [&] { return terminalCommand(dialect, reader, begin, offset); };
  auto operand = [&](u32 size) {
    return hasSequenceBytes(reader, offset, size, sequenceEnd);
  };

  if (status < 0x80) {
    if (!operand(1)) {
      return terminal();
    }
    ++offset;
    static_cast<void>(readVarLen(reader, offset, sequenceEnd));
    return recordFallthrough<Note>(dialect, reader, begin, offset);
  }

  switch (status) {
    case 0x80:
      static_cast<void>(readVarLen(reader, offset, sequenceEnd));
      return recordFallthrough<Rest>(dialect, reader, begin, offset);
    case 0x81:
      static_cast<void>(readVarLen(reader, offset, sequenceEnd));
      return recordFallthrough<Program>(dialect, reader, begin, offset);
    case 0x93:
      if (!operand(4)) {
        return terminal();
      }
      offset += 4;
      return recordFallthrough<NoOp>(dialect, reader, begin, offset);
    case 0x94: {
      const auto destination = readSseqAddress(reader, sequenceOffset, sequenceEnd, offset);
      if (!destination || *destination >= sequenceEnd) {
        return terminal();
      }
      offset += 3;
      auto decoded = recordCommand<Jump>(dialect, reader, begin, offset);
      decoded.flow.staticTargets = {Address{*destination}};
      return decoded;
    }
    case 0x95: {
      const auto destination = readSseqAddress(reader, sequenceOffset, sequenceEnd, offset);
      if (!destination || *destination >= sequenceEnd) {
        return terminal();
      }
      offset += 3;
      auto decoded = recordCommand<Call>(dialect, reader, begin, offset);
      decoded.flow.fallthrough = Address{offset};
      decoded.flow.staticTargets = {Address{*destination}};
      return decoded;
    }
    case 0x96:
      return terminalCommand(dialect, reader, begin, offset);
    case 0xa0:
      if (!operand(5)) {
        return terminal();
      }
      offset += 5;
      return recordFallthrough<NoOp>(dialect, reader, begin, offset);
    case 0xa1:
      if (!operand(2)) {
        return terminal();
      }
      offset += 2;
      return recordFallthrough<NoOp>(dialect, reader, begin, offset);
    case 0xa2:
      return recordFallthrough<NoOp>(dialect, reader, begin, offset);
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
      if (!operand(3)) {
        return terminal();
      }
      offset += 3;
      return recordFallthrough<NoOp>(dialect, reader, begin, offset);
    case 0xc0:
      if (!operand(1)) {
        return terminal();
      }
      return recordFallthrough<Pan>(dialect, reader, begin, offset + 1);
    case 0xc1:
      if (!operand(1)) {
        return terminal();
      }
      return recordFallthrough<Volume>(dialect, reader, begin, offset + 1);
    case 0xc3:
      if (!operand(1)) {
        return terminal();
      }
      return recordFallthrough<Transpose>(dialect, reader, begin, offset + 1);
    case 0xc4:
    case 0xc5:
    case 0xce:
    case 0xcf:
    case 0xd5:
      if (!operand(1)) {
        return terminal();
      }
      return recordFallthrough<OneByteNoOp>(dialect, reader, begin, offset + 1);
    case 0xc7:
      if (!operand(1)) {
        return terminal();
      }
      return recordFallthrough<NoteWait>(dialect, reader, begin, offset + 1);
    case 0xca:
      if (!operand(1)) {
        return terminal();
      }
      return recordFallthrough<OneByteNoOp>(dialect, reader, begin, offset + 1);
    case 0xe1:
      if (!operand(2)) {
        return terminal();
      }
      return recordFallthrough<Tempo>(dialect, reader, begin, offset + 2);
    case 0xe0:
    case 0xe3:
      if (!operand(2)) {
        return terminal();
      }
      offset += 2;
      return recordFallthrough<NoOp>(dialect, reader, begin, offset);
    case 0xfd:
      return recordCommand<Return>(dialect, reader, begin, offset);
    case 0xff:
      return recordCommand<End>(dialect, reader, begin, offset);
    case 0xfc:
      return recordFallthrough<NoOp>(dialect, reader, begin, offset);
    case 0xfe:
      if (!operand(2)) {
        return terminal();
      }
      offset += 2;
      return recordFallthrough<NoOp>(dialect, reader, begin, offset);
    default:
      if ((status >= 0xc2 && status <= 0xd6) && status != 0xc3 && status != 0xc4 && status != 0xc5 && status != 0xc7 &&
          status != 0xca && status != 0xce && status != 0xcf && status != 0xd5) {
        if (!operand(1)) {
          return terminal();
        }
        return recordFallthrough<OneByteNoOp>(dialect, reader, begin, offset + 1);
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
                NoOp, Terminal>();
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
  while (hasSequenceBytes(reader, offset, 1, sequenceEnd) && decodedCommands++ < kMaxTrackCommands) {
    auto decoded = decodeCommand(dialect, reader, sequenceOffset, sequenceEnd, offset);
    const u32 begin = offset;

    if (decoded.kind == handlerFor(dialect, Jump::kind).kind && !decoded.flow.staticTargets.empty()) {
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

    const auto next = decoded.flow.fallthrough;
    appendDecoded(builder, decoded, begin);
    if (!next || decoded.flow.terminal || decoded.kind == handlerFor(dialect, End::kind).kind) {
      break;
    }
    offset = next->value;
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

}  // namespace vgmtrans::formats::nds
