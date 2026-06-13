/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequenceDialect.h"

#include "value/core/BytecodeSequenceDecoder.h"
#include "value/core/BytecodeWalkers.h"
#include "value/core/LevelScale.h"
#include "value/core/SequenceVm.h"
#include "value/formats/NDS/NdsSequenceRecovery.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

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

// Notes, rests, and program selection.
struct Note {
  u8 key = 0;
  u8 velocity = 0;
  u32 duration = 0;

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
                LevelScale::linearFromMidi7(velocity), duration);
    return rt.wait(rt.state.noteWait ? duration : 0);
  }
};

struct Rest {
  u32 duration = 0;

  static Rest parse(CommandReader& in) { return Rest{.duration = in.varLen("duration")}; }

  Effects execute(Runtime& rt) const { return rt.wait(duration); }
};

struct Program {
  u32 raw = 0;

  static Program parse(CommandReader& in) { return Program{.raw = in.varLen("raw")}; }

  [[nodiscard]] u32 bank() const { return raw >> 7; }
  [[nodiscard]] u32 program() const { return raw & 0x7f; }

  void describe(CommandInfo& out) const {
    out.field("bank", bank());
    out.field("program", program());
  }

  void execute(Runtime& rt) const { rt.out.instrument(bank(), program()); }
};

// Control flow.
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
  [[nodiscard]] DecodeFlow decodeFlow(const BytecodeDecodeContext& context) const {
    const u32 destination = static_cast<u32>(context.sequenceOffset + 0x1c + relative);
    if (destination >= context.sequenceEnd) {
      return DecodeFlow::terminalFlow();
    }
    return DecodeFlow::jump(Address{destination});
  }

  Effects execute(Runtime& rt) const { return rt.jump(Address{absoluteAddress(rt.state, relative)}); }
};

struct Call : RelativeAddressCommand<Call> {
  [[nodiscard]] DecodeFlow decodeFlow(const BytecodeDecodeContext& context) const {
    const u32 destination = static_cast<u32>(context.sequenceOffset + 0x1c + relative);
    if (destination >= context.sequenceEnd) {
      return DecodeFlow::terminalFlow();
    }
    return DecodeFlow::call(Address{destination}, Address{context.commandEnd});
  }

  Effects execute(Runtime& rt) const { return rt.call(Address{absoluteAddress(rt.state, relative)}); }
};

struct Return : NoOperands<Return> {
  Effects execute(Runtime& rt) const { return rt.return_(); }
};

struct End : NoOperands<End> {
  Effects execute(Runtime& rt) const { return rt.end(); }
};

// Mixer, pitch, and performance controls.
struct Pan : U8Operand<Pan> {
  static constexpr std::string_view operandName = "pan";

  void execute(Runtime& rt) const { rt.out.pan(std::clamp((static_cast<double>(raw) / 63.5) - 1.0, -1.0, 1.0)); }
};

struct Volume : U8MidiLevelOutCommand<Volume, &Emit::level> {
  static constexpr std::string_view operandName = "volume";
};

struct ExpressionLevel : U8MidiLevelOutCommand<ExpressionLevel, &Emit::expression> {
  static constexpr std::string_view operandName = "expression";
};

struct Transpose {
  s8 semitones = 0;

  static Transpose parse(CommandReader& in) { return Transpose{.semitones = in.s8("semitones")}; }

  void execute(Runtime& rt) const { rt.state.transpose = semitones; }
};

struct PitchBend : S8Operand<PitchBend> {
  static constexpr std::string_view operandName = "bend";

  void execute(Runtime& rt) const { rt.out.pitchBend(static_cast<s16>(raw * 64)); }
};

struct PitchBendRange : U8RawOutCommand<PitchBendRange, &Emit::pitchBendRange> {
  static constexpr std::string_view operandName = "semitones";
};

struct ModulationDepth : U8MidiModulationOutCommand<ModulationDepth, ModulationPerformanceTarget::VibratoDepth> {
  static constexpr std::string_view operandName = "depth";
};

struct PortamentoSwitch : U8BoolOutCommand<PortamentoSwitch, &Emit::portamentoEnable> {
  static constexpr std::string_view operandName = "enabled";
};

struct PortamentoTime : U8RawOutCommand<PortamentoTime, &Emit::portamentoTime> {
  static constexpr std::string_view operandName = "time";
};

struct NoteWait : U8BoolStateCommand<NoteWait, &TrackState::noteWait> {
  static constexpr std::string_view operandName = "enabled";
};

struct Tempo {
  u16 bpm = 0;

  static Tempo parse(CommandReader& in) { return Tempo{.bpm = in.le16("bpm")}; }

  void execute(Runtime& rt) const {
    if (bpm != 0) {
      rt.out.tempo(static_cast<u32>(std::round(60000000.0 / bpm)));
    }
  }
};

// Stop conditions and diagnostics.
struct UnsupportedCommand : NoOperands<UnsupportedCommand> {
  Effects execute(Runtime& rt) const {
    rt.vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = "Unsupported NDS SSEQ command stopped playback",
    });
    return rt.end();
  }
};

struct UnknownOpcode {
  static UnknownOpcode parse(CommandReader& in) {
    in.derived("opcode", static_cast<u64>(in.opcode()));
    return {};
  }

  Effects execute(Runtime& rt) const {
    rt.vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = "Unknown NDS SSEQ opcode stopped playback",
    });
    return rt.end();
  }
};

struct TruncatedCommand : NoOperands<TruncatedCommand> {
  Effects execute(Runtime& rt) const {
    rt.vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = "Truncated NDS SSEQ command stopped playback",
    });
    return rt.end();
  }
};

struct NdsBytecodeMap {
  BytecodeDispatchTable dispatch;
  BytecodeCommandSpec noOp;
};

template <class Registrar>
[[nodiscard]] NdsBytecodeMap ndsBytecodeMap(Registrar& registrar) {
  BytecodeMapBuilder<TrackState, Context> map{"nds", registrar};

  constexpr std::array preservedCommands{
      preservedOpcode(0x93, "Open Track", operandBytes(4)),
      preservedOpcode(0xa0, "Cmd with Random Value", operandBytes(5), suffix("random-value")),
      preservedOpcode(0xa1, "Cmd with Variable", operandBytes(2), suffix("variable-command")),
      preservedOpcode(0xa2, "If"),
      preservedOpcode(0xb0, "Set Variable", operandBytes(3)),
      preservedOpcode(0xb1, "Add Variable", operandBytes(3)),
      preservedOpcode(0xb2, "Sub Variable", operandBytes(3)),
      preservedOpcode(0xb3, "Mul Variable", operandBytes(3)),
      preservedOpcode(0xb4, "Div Variable", operandBytes(3)),
      preservedOpcode(0xb5, "Shift Variable", operandBytes(3)),
      preservedOpcode(0xb6, "Rand Variable", operandBytes(3)),
      preservedOpcode(0xb8, "If Variable ==", operandBytes(3), suffix("if-variable-equal")),
      preservedOpcode(0xb9, "If Variable >=", operandBytes(3), suffix("if-variable-greater-equal")),
      preservedOpcode(0xba, "If Variable >", operandBytes(3), suffix("if-variable-greater")),
      preservedOpcode(0xbb, "If Variable <=", operandBytes(3), suffix("if-variable-less-equal")),
      preservedOpcode(0xbc, "If Variable <", operandBytes(3), suffix("if-variable-less")),
      preservedOpcode(0xbd, "If Variable !=", operandBytes(3), suffix("if-variable-not-equal")),
      preservedOpcode(0xc2, "Master Volume", operandBytes(1)),
      preservedOpcode(0xc6, "Priority", operandBytes(1)),
      preservedOpcode(0xc8, "Tie", operandBytes(1)),
      preservedOpcode(0xc9, "Portamento Control", operandBytes(1)),
      preservedOpcode(0xcb, "Modulation Speed", operandBytes(1)),
      preservedOpcode(0xcc, "Modulation Type", operandBytes(1)),
      preservedOpcode(0xcd, "Modulation Range", operandBytes(1)),
      preservedOpcode(0xd0, "Attack Rate", operandBytes(1)),
      preservedOpcode(0xd1, "Decay Rate", operandBytes(1)),
      preservedOpcode(0xd2, "Sustain Level", operandBytes(1)),
      preservedOpcode(0xd3, "Release Rate", operandBytes(1)),
      preservedOpcode(0xd4, "Loop Start", operandBytes(1)),
      preservedOpcode(0xd6, "Print Variable", operandBytes(1)),
      preservedOpcode(0xe0, "Modulation Delay", operandBytes(2)),
      preservedOpcode(0xe3, "Sweep Pitch", operandBytes(2)),
      preservedOpcode(0xfc, "Loop End"),
      preservedOpcode(0xfe, "Allocate Track", operandBytes(2)),
  };

  map.range<0x00, 0x7f, Note>("Note");
  map.op<0x80, Rest>("Rest");
  map.op<0x81, Program>("Program");
  map.op<0x94, Jump>("Jump");
  map.op<0x95, Call>("Call");
  map.terminal<0x96, UnsupportedCommand>("Unsupported Command", suffix("unsupported"));
  map.preserved(preservedCommands);
  map.op<0xc0, Pan>("Pan");
  map.op<0xc1, Volume>("Volume");
  map.op<0xc3, Transpose>("Transpose");
  map.op<0xc4, PitchBend>("Pitch Bend");
  map.op<0xc5, PitchBendRange>("Pitch Bend Range");
  map.op<0xc7, NoteWait>("Note Wait");
  map.op<0xca, ModulationDepth>("Modulation Depth");
  map.op<0xce, PortamentoSwitch>("Portamento");
  map.op<0xcf, PortamentoTime>("Portamento Time");
  map.op<0xd5, ExpressionLevel>("Expression");
  map.op<0xe1, Tempo>("Tempo");
  map.returns<0xfd, Return>("Return");
  map.terminal<0xff, End>("End");
  map.truncated<TruncatedCommand>("Truncated Command", suffix("truncated"));
  map.unknown<UnknownOpcode>("Unknown Opcode", suffix("unknown"));

  BytecodeCommandSpec noOp = map.preservedSpec("No-op", operandBytes(0), suffix("no-op"));
  return NdsBytecodeMap{
      .dispatch = map.finish(),
      .noOp = std::move(noOp),
  };
}

// Normal SSEQ decode follows statically reachable bytecode blocks from the
// track start, preserving calls and jumps as source commands.
[[nodiscard]] TrackProgram decodeReachableBlocks(ByteReader reader, const NdsBytecodeMap& bytecode, u32 sequenceOffset,
                                                 u32 sequenceEnd, u32 startOffset, u32 trackIndex) {
  return decodeReachableBytecodeBlocks(
      reader, sequenceEnd, startOffset, trackIndex,
      ReachableBytecodeDecodePolicy{.maxCommands = static_cast<u32>(kMaxTrackCommands)}, [&](u32 offset) {
        return bytecode.dispatch.decode(reader, offset,
                                        BytecodeDecodeContext{
                                            .bytecodeEnd = sequenceEnd,
                                            .sequenceOffset = sequenceOffset,
                                            .sequenceEnd = sequenceEnd,
                                        });
      });
}

[[nodiscard]] const NdsBytecodeMap& ndsBytecodeMapFor(const SequenceDialect& dialect) {
  static const NdsBytecodeMap bytecode = ndsBytecodeMap(dialect);
  return bytecode;
}

}  // namespace

SequenceDialect ndsSequenceDialect() {
  SequenceDialectBuilder<TrackState, Context> builder{kNdsSequenceDialectId, Context{}};
  builder.timebase(Timebase{.ppqn = 0x30})
      .defaultBehavior(SequenceProgramBehavior{
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
          .commandLimit = static_cast<u32>(kMaxTrackCommands),
      });
  static_cast<void>(ndsBytecodeMap(builder));
  return builder.finish();
}

void registerNdsSequenceDialect(SequenceDialectRegistry& registry) {
  registry.add(ndsSequenceDialect());
}

TrackProgram decodeNdsSequenceTrack(ByteReader reader, const SequenceDialect& dialect, u32 sequenceOffset,
                                    u32 sequenceEnd, u32 startOffset, u32 trackIndex, bool recoverMalformedSdatRange) {
  const NdsBytecodeMap& bytecode = ndsBytecodeMapFor(dialect);
  if (recoverMalformedSdatRange) {
    return decodeMalformedSdatRangeTrack(reader, bytecode.dispatch, bytecode.noOp, *bytecode.dispatch.opcodes[0xff],
                                         sequenceOffset, sequenceEnd, startOffset, trackIndex, kMaxTrackCommands);
  }
  return decodeReachableBlocks(reader, bytecode, sequenceOffset, sequenceEnd, startOffset, trackIndex);
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

}  // namespace vgmtrans::formats::nds
