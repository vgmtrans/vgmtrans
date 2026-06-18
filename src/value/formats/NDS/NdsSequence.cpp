/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequence.h"

#include "value/base/LevelScale.h"
#include "value/sequence/SequenceVm.h"
#include "value/sequence/bytecode/BytecodeMap.h"
#include "value/sequence/bytecode/BytecodeWalkers.h"

#include <fmt/format.h>

#include <algorithm>
#include <any>
#include <array>
#include <cstddef>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr size_t kMaxTrackCommands = 262144;
constexpr std::string_view kSseqSignature{"SSEQ\xff\xfe\x00\x01", 8};

struct Context {};

struct PendingBlock {
  u32 offset = 0;
  bool callTarget = false;
};

struct TrackState {
  TrackState() = default;

  explicit TrackState(const SequenceProgram& program, const TrackProgram&)
      : sequenceDataBase(program.sourceBaseAddress.value) {}

  explicit TrackState(const BytecodeDecodeContext& context)
      : sequenceDataBase(context.sequenceOffset + 0x1c), sequenceEnd(context.sequenceEnd) {}

  u64 sequenceDataBase = 0;
  u32 sequenceEnd = std::numeric_limits<u32>::max();
  bool noteWait = false;
  s32 transpose = 0;
  u8 pitchBendRangeSemitones = 2;
};

[[nodiscard]] bool matches(ByteReader reader, u64 offset, std::string_view signature) {
  if (!reader.has(offset, signature.size())) {
    return false;
  }
  for (size_t i = 0; i < signature.size(); ++i) {
    if (reader.u8At(offset + i) != static_cast<u8>(signature[i])) {
      return false;
    }
  }
  return true;
}

// Track-start discovery sometimes needs to skip a bootstrap rest command before
// the real primary track. Keep offset unchanged if the variable-length value is
// truncated so normal bytecode decode can still preserve the bad command.
[[nodiscard]] std::optional<u32> readVarLen(ByteReader reader, u32& offset, u32 sequenceEnd) {
  u32 value = 0;
  u32 cursor = offset;
  while (hasBytecodeBytes(reader, cursor, 1, sequenceEnd)) {
    const u8 byte = reader.u8At(cursor++);
    value = (value << 7) + (byte & 0x7f);
    if ((byte & 0x80) == 0) {
      offset = cursor;
      return value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<u32> readSseqAddress(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd,
                                                 u32 operandOffset) {
  if (!hasBytecodeBytes(reader, operandOffset, 3, sequenceEnd)) {
    return std::nullopt;
  }
  return reader.u8At(operandOffset) + (reader.u8At(operandOffset + 1) << 8) + (reader.u8At(operandOffset + 2) << 16) +
         sequenceOffset + 0x1c;
}

[[nodiscard]] std::optional<u32> nearbySseqHeader(ByteReader reader, u32 offset, u32 size) {
  constexpr u32 kMaxPaddingBeforeSseq = 0x200;
  const u64 searchEnd = std::min<u64>(reader.size(), static_cast<u64>(offset) + size + kMaxPaddingBeforeSseq);
  for (u64 candidate = offset + 1; candidate + kSseqSignature.size() <= searchEnd; ++candidate) {
    if (matches(reader, candidate, kSseqSignature)) {
      return static_cast<u32>(candidate);
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool isZeroFilled(ByteReader reader, u32 begin, u32 end) {
  for (u32 offset = begin; offset < end && reader.has(offset, 1); ++offset) {
    if (reader.u8At(offset) != 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u32> recoveredMalformedSdatSequenceOffset(ByteReader reader, u32 offset, u32 size) {
  const auto sseqOffset = nearbySseqHeader(reader, offset, size);
  if (!sseqOffset) {
    return std::nullopt;
  }

  const u32 trackStart = offset + 0x1c;
  const u32 paddingEnd = std::min(*sseqOffset, offset + size);
  // Some zero-filled pseudo-sequences overlap a later SSEQ. If the padding
  // would align the SSEQ signature as bogus note data, leave it empty.
  if (size <= 0x100 && *sseqOffset >= trackStart && isZeroFilled(reader, offset, paddingEnd) &&
      ((*sseqOffset - trackStart) % 3) == 2) {
    return std::nullopt;
  }
  return sseqOffset;
}

template <class Runtime>
void renderWarning(Runtime& rt, std::string message) {
  if constexpr (requires { rt.vm.diagnostic(Diagnostic{}); }) {
    rt.vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = std::move(message),
    });
  }
}

template <class Runtime>
[[nodiscard]] bool decodeTargetOutsideSequence(VmCommandCursor& cmd, Runtime& rt, Address destination) {
  return cmd.phase() == CommandPhase::Decode && rt.state.sequenceEnd != std::numeric_limits<u32>::max() &&
         destination.value >= rt.state.sequenceEnd;
}

struct NdsCursorReader {
  template <class Runtime>
  static CommandFlow read(VmCommandCursor& cmd, Runtime& rt) {
    const u8 opcode = cmd.opcode();
    if (opcode <= 0x7f) {
      return note(cmd, rt, opcode);
    }

    switch (opcode) {
      case 0x80:
        return rest(cmd);
      case 0x81:
        return program(cmd, rt);
      case 0x94:
        return jump(cmd, rt);
      case 0x95:
        return call(cmd, rt);
      case 0x96:
        return unsupported(cmd, rt, "Unsupported NDS SSEQ command stopped playback");
      case 0xc0:
        return pan(cmd, rt);
      case 0xc1:
        return volume(cmd, rt);
      case 0xc3:
        return transpose(cmd, rt);
      case 0xc4:
        return pitchBend(cmd, rt);
      case 0xc5:
        return pitchBendRange(cmd, rt);
      case 0xc7:
        return noteWait(cmd, rt);
      case 0xca:
        return modulationDepth(cmd, rt);
      case 0xce:
        return portamentoSwitch(cmd, rt);
      case 0xcf:
        return portamentoTime(cmd, rt);
      case 0xd5:
        return expression(cmd, rt);
      case 0xe1:
        return tempo(cmd, rt);
      case 0xfd:
        return cmd.name("Return").kind("return").semantic(SequenceSemantic::Return).ret();
      case 0xff:
        return cmd.name("End").kind("end").semantic(SequenceSemantic::End).end();
      default:
        return unsupported(cmd, rt, "Unknown NDS SSEQ opcode stopped playback", "Unknown Opcode", "unknown");
    }
  }

private:
  template <class Runtime>
  static CommandFlow note(VmCommandCursor& cmd, Runtime& rt, u8 key) {
    cmd.name("Note").semantic(SequenceSemantic::Note).derived("key", static_cast<u64>(key), SourceValueDisplay::MidiNote);
    const auto velocity = cmd.u8("velocity");
    const auto duration = cmd.varLen("duration");
    if (velocity && duration) {
      rt.note(static_cast<double>(std::clamp<s32>(static_cast<s32>(key) + rt.state.transpose, 0, 127)),
              LevelScale::linearFromMidi7(velocity.value), duration.value);
    }
    return rt.state.noteWait ? cmd.wait(duration.value) : cmd.next();
  }

  static CommandFlow rest(VmCommandCursor& cmd) {
    cmd.name("Rest").semantic(SequenceSemantic::Rest);
    const auto duration = cmd.varLen("duration");
    return cmd.wait(duration.value);
  }

  template <class Runtime>
  static CommandFlow program(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Program").semantic(SequenceSemantic::Program);
    const auto raw = cmd.varLen("raw");
    const u32 bank = raw.value >> 7;
    const u32 program = raw.value & 0x7f;
    cmd.derived("bank", static_cast<u64>(bank)).derived("program", static_cast<u64>(program));
    if (raw) {
      rt.instrument(bank, program);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow jump(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Jump").semantic(SequenceSemantic::Jump);
    const auto destination = cmd.le24RelativeAddress("destination", Address{static_cast<u32>(rt.state.sequenceDataBase)});
    if (!destination || decodeTargetOutsideSequence(cmd, rt, destination.value)) {
      return cmd.end();
    }
    return cmd.jump(destination.value);
  }

  template <class Runtime>
  static CommandFlow call(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Call").semantic(SequenceSemantic::Call);
    const auto destination = cmd.le24RelativeAddress("destination", Address{static_cast<u32>(rt.state.sequenceDataBase)});
    if (!destination || decodeTargetOutsideSequence(cmd, rt, destination.value)) {
      return cmd.end();
    }
    return cmd.call(destination.value);
  }

  template <class Runtime>
  static CommandFlow pan(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Pan").semantic(SequenceSemantic::Pan);
    const auto raw = cmd.u8("pan");
    if (raw) {
      rt.pan(std::clamp((static_cast<double>(raw.value) / 63.5) - 1.0, -1.0, 1.0));
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow volume(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Volume").semantic(SequenceSemantic::Level);
    const auto raw = cmd.u8("volume");
    if (raw) {
      rt.level(LevelScale::linearFromMidi7(raw.value));
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow expression(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Expression").kind("expression").semantic(SequenceSemantic::Level);
    const auto raw = cmd.u8("expression");
    if (raw) {
      rt.expression(LevelScale::linearFromMidi7(raw.value));
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow transpose(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Transpose").semantic(SequenceSemantic::State);
    const auto semitones = cmd.s8("semitones");
    if (semitones) {
      rt.state.transpose = semitones.value;
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow pitchBend(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Pitch Bend").semantic(SequenceSemantic::Pitch);
    const auto bend = cmd.s8("bend");
    if (bend) {
      rt.pitchBend((static_cast<double>(bend.value) / 128.0) * rt.state.pitchBendRangeSemitones);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow pitchBendRange(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Pitch Bend Range").semantic(SequenceSemantic::Pitch);
    const auto semitones = cmd.u8("semitones");
    if (semitones) {
      rt.state.pitchBendRangeSemitones = semitones.value;
      rt.pitchBendRange(semitones.value);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow noteWait(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Note Wait").semantic(SequenceSemantic::State);
    const auto enabled = cmd.u8("enabled");
    if (enabled) {
      rt.state.noteWait = enabled.value != 0;
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow modulationDepth(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Modulation Depth").semantic(SequenceSemantic::Modulation);
    const auto depth = cmd.u8("depth");
    if (depth) {
      rt.modulation(ModulationPerformanceTarget::VibratoDepth,
                    std::clamp(static_cast<double>(depth.value) / 127.0, 0.0, 1.0));
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow portamentoSwitch(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Portamento").semantic(SequenceSemantic::Portamento);
    const auto enabled = cmd.u8("enabled");
    if (enabled) {
      rt.portamentoEnable(enabled.value != 0);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow portamentoTime(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Portamento Time").semantic(SequenceSemantic::Portamento);
    const auto time = cmd.u8("time");
    if (time) {
      rt.portamentoTime(static_cast<double>(time.value));
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow tempo(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Tempo").semantic(SequenceSemantic::Tempo);
    const auto bpm = cmd.u16le("bpm");
    if (bpm && bpm.value != 0) {
      rt.tempo(static_cast<u32>(std::round(60000000.0 / bpm.value)));
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow unsupported(VmCommandCursor& cmd, Runtime& rt, std::string_view message,
                                 std::string_view name = "Unsupported Command",
                                 std::string_view kind = "unsupported") {
    cmd.name(name).kind(kind).semantic(SequenceSemantic::Unsupported).unsupported(message);
    renderWarning(rt, std::string(message));
    return cmd.end();
  }
};

using NdsCursorCommand = CursorBytecodeCommand<TrackState, Context, NdsCursorReader>;

[[nodiscard]] TrackProgram makeTrack(u32 startOffset, u32 trackIndex) {
  return TrackProgram{
      .id = TrackId{trackIndex},
      .sourceTrackNumber = trackIndex,
      .startAddress = Address{startOffset},
  };
}

[[nodiscard]] DecodedBytecodeCommand terminalRecoveryCommand(const BytecodeCommandSpec& terminalSpec, ByteReader reader,
                                                             u32 offset) {
  auto command = recordSizedPreservedBytecodeCommand(terminalSpec, reader, offset, offset + 1);
  command.flow = DecodeFlow::terminalFlow();
  return command;
}

// Recovery decoder for malformed SDAT ranges that do not contain a normal SSEQ
// header. Normal SSEQ decode stays source-driver oriented; this path repairs
// range-level damage before source commands can be trusted.
[[nodiscard]] TrackProgram decodeMalformedSdatRangeTrack(ByteReader reader, const BytecodeDispatchTable& dispatch,
                                                         const BytecodeCommandSpec& terminalSpec,
                                                         const std::any& dialectContext, u32 sequenceOffset,
                                                         u32 sequenceEnd, u32 startOffset, u32 trackIndex,
                                                         size_t maxCommands, SourceMapBuilder* sourceMap,
                                                         std::vector<Diagnostic>* diagnostics) {
  TrackProgram track = makeTrack(startOffset, trackIndex);
  TrackProgramBuilder builder{track};
  u32 offset = startOffset;
  size_t decodedCommands = 0;
  std::set<u32> decodedOffsets;
  std::set<u32> callTargetOffsets;
  std::vector<PendingBlock> pendingBlocks{{.offset = startOffset}};

  while (!pendingBlocks.empty()) {
    const PendingBlock block = pendingBlocks.back();
    pendingBlocks.pop_back();
    offset = block.offset;

    while (hasBytecodeBytes(reader, offset, 1, sequenceEnd) && decodedCommands++ < maxCommands) {
      const u32 begin = offset;
      if (decodedOffsets.contains(begin)) {
        break;
      }
      decodedOffsets.insert(begin);

      auto decoded = dispatch.decode(reader, offset,
                                     BytecodeDecodeContext{
                                         .bytecodeEnd = sequenceEnd,
                                         .sequenceOffset = sequenceOffset,
                                         .sequenceEnd = sequenceEnd,
                                         .dialectContext = &dialectContext,
                                         .sourceMap = sourceMap,
                                         .diagnostics = diagnostics,
                                     });

      if (!block.callTarget) {
        const auto overlap = std::ranges::find_if(
            callTargetOffsets, [&](u32 target) { return begin < target && target < decoded.range.endOffset(); });
        if (overlap != callTargetOffsets.end()) {
          // Some malformed FAT entries fall through one byte before a real call
          // target. Stop before consuming the overlapping subroutine bytes.
          appendDecodedBytecodeCommand(builder, terminalRecoveryCommand(terminalSpec, reader, begin), begin);
          break;
        }
      }

      if (decoded.flow.unconditionalJump()) {
        const u32 destination = decoded.flow.staticTargets.front().value;
        appendDecodedBytecodeCommand(builder, decoded, begin);
        if (decodedOffsets.contains(destination)) {
          break;
        }
        offset = destination;
        continue;
      }

      if (decoded.flow.callTarget()) {
        const u32 destination = decoded.flow.staticTargets.front().value;
        if (!decodedOffsets.contains(destination) && callTargetOffsets.insert(destination).second) {
          pendingBlocks.push_back(PendingBlock{.offset = destination, .callTarget = true});
        }
      }

      const auto next = decoded.flow.fallthrough;
      appendDecodedBytecodeCommand(builder, decoded, begin);
      if (!next || decoded.flow.terminal) {
        break;
      }
      offset = next->value;
    }
  }

  return track;
}

template <class Registrar>
[[nodiscard]] BytecodeDispatchTable ndsBytecodeMap(Registrar& registrar) {
  BytecodeMapBuilder<TrackState, Context> map{"nds", registrar};

  constexpr std::array preservedCommands{
      preservedOpcode(0x93, "Open Track", operandBytes(4)),
      preservedOpcode(0xa0, commandMeta("random-value", "Cmd with Random Value"), operandBytes(5)),
      preservedOpcode(0xa1, commandMeta("variable-command", "Cmd with Variable"), operandBytes(2)),
      preservedOpcode(0xa2, "If"),
      preservedOpcode(0xb0, "Set Variable", operandBytes(3)),
      preservedOpcode(0xb1, "Add Variable", operandBytes(3)),
      preservedOpcode(0xb2, "Sub Variable", operandBytes(3)),
      preservedOpcode(0xb3, "Mul Variable", operandBytes(3)),
      preservedOpcode(0xb4, "Div Variable", operandBytes(3)),
      preservedOpcode(0xb5, "Shift Variable", operandBytes(3)),
      preservedOpcode(0xb6, "Rand Variable", operandBytes(3)),
      preservedOpcode(0xb8, commandMeta("if-variable-equal", "If Variable =="), operandBytes(3)),
      preservedOpcode(0xb9, commandMeta("if-variable-greater-equal", "If Variable >="), operandBytes(3)),
      preservedOpcode(0xba, commandMeta("if-variable-greater", "If Variable >"), operandBytes(3)),
      preservedOpcode(0xbb, commandMeta("if-variable-less-equal", "If Variable <="), operandBytes(3)),
      preservedOpcode(0xbc, commandMeta("if-variable-less", "If Variable <"), operandBytes(3)),
      preservedOpcode(0xbd, commandMeta("if-variable-not-equal", "If Variable !="), operandBytes(3)),
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

  map.cursorRange<0x00, 0x7f, NdsCursorCommand>(commandMeta("note", "Note"));
  map.cursorOp<0x80, NdsCursorCommand>(commandMeta("rest", "Rest"));
  map.cursorOp<0x81, NdsCursorCommand>(commandMeta("program", "Program"));
  map.cursorOp<0x94, NdsCursorCommand>(commandMeta("jump", "Jump"),
                                       BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::AffectsControlFlow});
  map.cursorOp<0x95, NdsCursorCommand>(commandMeta("call", "Call"),
                                       BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::AffectsControlFlow});
  map.cursorOp<0x96, NdsCursorCommand>(
      commandMeta("unsupported-command", "Unsupported Command"),
      BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::Unsupported});
  map.preserved(preservedCommands);
  map.cursorOp<0xc0, NdsCursorCommand>(commandMeta("pan", "Pan"));
  map.cursorOp<0xc1, NdsCursorCommand>(commandMeta("volume", "Volume"));
  map.cursorOp<0xc3, NdsCursorCommand>(commandMeta("transpose", "Transpose"));
  map.cursorOp<0xc4, NdsCursorCommand>(commandMeta("pitch-bend", "Pitch Bend"));
  map.cursorOp<0xc5, NdsCursorCommand>(commandMeta("pitch-bend-range", "Pitch Bend Range"));
  map.cursorOp<0xc7, NdsCursorCommand>(commandMeta("note-wait", "Note Wait"));
  map.cursorOp<0xca, NdsCursorCommand>(commandMeta("modulation-depth", "Modulation Depth"));
  map.cursorOp<0xce, NdsCursorCommand>(commandMeta("portamento", "Portamento"));
  map.cursorOp<0xcf, NdsCursorCommand>(commandMeta("portamento-time", "Portamento Time"));
  map.cursorOp<0xd5, NdsCursorCommand>(commandMeta("expression", "Expression"));
  map.cursorOp<0xe1, NdsCursorCommand>(commandMeta("tempo", "Tempo"));
  map.cursorOp<0xfd, NdsCursorCommand>(commandMeta("return", "Return"),
                                       BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::AffectsControlFlow});
  map.cursorOp<0xff, NdsCursorCommand>(commandMeta("end", "End"),
                                       BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::StopsPlayback});
  map.cursorTruncated<NdsCursorCommand>(
      commandMeta("truncated", "Truncated Command"),
      BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::Unsupported});
  map.cursorUnknown<NdsCursorCommand>(
      commandMeta("unknown", "Unknown Opcode"),
      BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::Unsupported});

  return map.finish();
}

// Normal SSEQ decode follows statically reachable bytecode blocks from the
// track start, preserving calls and jumps as source commands.
[[nodiscard]] TrackProgram decodeReachableBlocks(ByteReader reader, const BytecodeDispatchTable& bytecode,
                                                 const std::any& dialectContext, u32 sequenceOffset, u32 sequenceEnd,
                                                 u32 startOffset, u32 trackIndex, SourceMapBuilder* sourceMap,
                                                 std::vector<Diagnostic>* diagnostics) {
  return decodeReachableBytecodeBlocks(
      reader, sequenceEnd, startOffset, trackIndex,
      ReachableBytecodeDecodePolicy{.maxCommands = static_cast<u32>(kMaxTrackCommands)}, [&](u32 offset) {
        return bytecode.decode(reader, offset,
                               BytecodeDecodeContext{
                                   .bytecodeEnd = sequenceEnd,
                                   .sequenceOffset = sequenceOffset,
                                   .sequenceEnd = sequenceEnd,
                                   .dialectContext = &dialectContext,
                                   .sourceMap = sourceMap,
                                   .diagnostics = diagnostics,
                               });
      });
}

[[nodiscard]] NdsSequenceDescriptor makeNdsSequenceDescriptor() {
  SequenceDialectBuilder<TrackState, Context> builder{kNdsSequenceDialectId, Context{}};
  builder.timebase(Timebase{.ppqn = 0x30})
      .defaultBehavior(SequenceProgramBehavior{
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
          .commandLimit = static_cast<u32>(kMaxTrackCommands),
      });
  auto bytecode = ndsBytecodeMap(builder);
  return NdsSequenceDescriptor{
      .dialect = builder.finish(),
      .bytecode = std::move(bytecode),
  };
}

}  // namespace

const NdsSequenceDescriptor& ndsSequenceDescriptor() {
  static const NdsSequenceDescriptor descriptor = makeNdsSequenceDescriptor();
  return descriptor;
}

SequenceDialect ndsSequenceDialect() {
  return ndsSequenceDescriptor().dialect;
}

void registerNdsSequenceDialect(SequenceDialectRegistry& registry) {
  registry.add(ndsSequenceDescriptor().dialect);
}

TrackProgram decodeNdsSequenceTrack(ByteReader reader, const NdsSequenceDescriptor& descriptor, u32 sequenceOffset,
                                    u32 sequenceEnd, u32 startOffset, u32 trackIndex, bool recoverMalformedSdatRange,
                                    SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const BytecodeDispatchTable& bytecode = descriptor.bytecode;
  if (recoverMalformedSdatRange) {
    return decodeMalformedSdatRangeTrack(reader, bytecode, *bytecode.opcodes[0xff], descriptor.dialect.context,
                                         sequenceOffset, sequenceEnd, startOffset, trackIndex, kMaxTrackCommands,
                                         sourceMap, diagnostics);
  }
  return decodeReachableBlocks(reader, bytecode, descriptor.dialect.context, sequenceOffset, sequenceEnd, startOffset,
                               trackIndex, sourceMap, diagnostics);
}

std::vector<u32> ndsSequenceTrackStarts(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd) {
  std::vector<u32> extraStarts;
  u32 offset = sequenceOffset + 0x1c;
  if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
    return {offset};
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
      const u32 statusOffset = offset;
      ++offset;
      if (!readVarLen(reader, offset, sequenceEnd)) {
        return {statusOffset};
      }
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

NdsSequenceRange ndsSequenceRangeForFatEntry(ByteReader reader, u32 offset, u32 size) {
  const bool hasSseqHeader = matches(reader, offset, kSseqSignature);
  const u32 fatEnd = static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + size));
  const std::optional<u32> recoveredSequenceOffset =
      hasSseqHeader ? std::nullopt : recoveredMalformedSdatSequenceOffset(reader, offset, size);
  const bool recoverMalformedSdatRange = recoveredSequenceOffset.has_value();
  const bool zeroFilled = !hasSseqHeader && !recoverMalformedSdatRange && isZeroFilled(reader, offset, fatEnd);
  const u32 decodeOffset = recoveredSequenceOffset.value_or(offset);
  const u32 recoveredEnd =
      recoveredSequenceOffset && reader.has(*recoveredSequenceOffset + 8, 4)
          ? static_cast<u32>(std::min<u64>(
                reader.size(), static_cast<u64>(*recoveredSequenceOffset) + reader.le32(*recoveredSequenceOffset + 8)))
          : static_cast<u32>(reader.size());
  const u32 emptySequenceEnd = static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + 0x1c));
  const u32 sequenceEnd = zeroFilled ? emptySequenceEnd : recoverMalformedSdatRange ? recoveredEnd : fatEnd;
  return NdsSequenceRange{
      .offset = offset,
      .decodeOffset = decodeOffset,
      .size = size,
      .sequenceEnd = sequenceEnd,
      .recoverMalformedSdatRange = recoverMalformedSdatRange,
  };
}

SequenceProgramAsset parseNdsSequenceProgram(const ScanInput& input, AssetId id, NdsSequenceRange range,
                                             const std::string& name, std::optional<ScanInstrumentSetRef> instrumentSet,
                                             SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const NdsSequenceDescriptor& descriptor = ndsSequenceDescriptor();
  const SequenceDialect& dialect = descriptor.dialect;
  const u32 sequenceOffset = range.decodeOffset != 0 ? range.decodeOffset : range.offset;
  const std::optional<AssetId> instrumentSetId =
      instrumentSet ? std::optional<AssetId>{instrumentSet->id} : std::nullopt;
  SequenceProgramAsset asset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kNdsFormatName),
              .name = name,
              .range = input.reader.range(sequenceOffset, range.sequenceEnd - sequenceOffset),
          },
      .program =
          SequenceProgram{
              .dialect = dialect.id,
              .timebase = dialect.timebase,
              .sourceBaseAddress = Address{sequenceOffset + 0x1c},
              .behavior = dialect.defaultBehavior,
          },
  };

  ItemTreeBuilder items(asset.metadata.items, input.ids);
  const auto root = items.add(std::nullopt, ItemKind::Sequence, "sseq", name,
                              input.reader.range(sequenceOffset, range.sequenceEnd - sequenceOffset));

  u32 trackIndex = 0;
  for (const u32 start : ndsSequenceTrackStarts(input.reader, sequenceOffset, range.sequenceEnd)) {
    auto track = decodeNdsSequenceTrack(input.reader, descriptor, sequenceOffset, range.sequenceEnd, start,
                                        trackIndex++, range.recoverMalformedSdatRange, sourceMap, diagnostics);
    const auto trackItem = items.add(root, ItemKind::Track, "track", fmt::format("Track {}", track.sourceTrackNumber),
                                     input.reader.range(start, 0));
    addSourceCommandItemsAndInstrumentReferences(items, trackItem, asset.program, dialect, track, instrumentSetId);
    asset.program.tracks.push_back(std::move(track));
  }

  return asset;
}

}  // namespace vgmtrans::formats::nds
