/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequence.h"

#include "value/base/LevelScale.h"
#include "value/sequence/SequenceVm.h"
#include "value/sequence/bytecode/BytecodeSequenceDecoder.h"
#include "value/sequence/bytecode/BytecodeWalkers.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
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

struct Volume : U8MidiLevelOutCommand<Volume, &PerformanceEmitter::level> {
  static constexpr std::string_view operandName = "volume";
};

struct ExpressionLevel : U8MidiLevelOutCommand<ExpressionLevel, &PerformanceEmitter::expression> {
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

struct PitchBendRange : U8RawOutCommand<PitchBendRange, &PerformanceEmitter::pitchBendRange> {
  static constexpr std::string_view operandName = "semitones";
};

struct ModulationDepth : U8MidiModulationOutCommand<ModulationDepth, ModulationPerformanceTarget::VibratoDepth> {
  static constexpr std::string_view operandName = "depth";
};

struct PortamentoSwitch : U8BoolOutCommand<PortamentoSwitch, &PerformanceEmitter::portamentoEnable> {
  static constexpr std::string_view operandName = "enabled";
};

struct PortamentoTime : U8RawOutCommand<PortamentoTime, &PerformanceEmitter::portamentoTime> {
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
};

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
                                                         const BytecodeCommandSpec& terminalSpec, u32 sequenceOffset,
                                                         u32 sequenceEnd, u32 startOffset, u32 trackIndex,
                                                         size_t maxCommands) {
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

  return NdsBytecodeMap{
      .dispatch = map.finish(),
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
    return decodeMalformedSdatRangeTrack(reader, bytecode.dispatch, *bytecode.dispatch.opcodes[0xff], sequenceOffset,
                                         sequenceEnd, startOffset, trackIndex, kMaxTrackCommands);
  }
  return decodeReachableBlocks(reader, bytecode, sequenceOffset, sequenceEnd, startOffset, trackIndex);
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

NdsSequenceRange ndsSequenceRangeForFatEntry(ByteReader reader, u32 offset, u32 size) {
  const bool hasSseqHeader = matches(reader, offset, kSseqSignature);
  const u32 fatEnd = static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + size));
  const std::optional<u32> recoveredSequenceOffset =
      hasSseqHeader ? std::nullopt : recoveredMalformedSdatSequenceOffset(reader, offset, size);
  const bool recoverMalformedSdatRange = recoveredSequenceOffset.has_value();
  const bool zeroFilled = !hasSseqHeader && !recoverMalformedSdatRange && isZeroFilled(reader, offset, fatEnd);
  const u32 decodeOffset = recoveredSequenceOffset.value_or(offset);
  const u32 recoveredEnd = recoveredSequenceOffset && reader.has(*recoveredSequenceOffset + 8, 4)
                               ? static_cast<u32>(std::min<u64>(
                                     reader.size(), static_cast<u64>(*recoveredSequenceOffset) +
                                                        reader.le32(*recoveredSequenceOffset + 8)))
                               : static_cast<u32>(reader.size());
  const u32 emptySequenceEnd =
      static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + 0x1c));
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
                                             const std::string& name, std::optional<AssetId> instrumentSet) {
  const SequenceDialect dialect = ndsSequenceDialect();
  const u32 sequenceOffset = range.decodeOffset != 0 ? range.decodeOffset : range.offset;
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

  const CommandHandler* programHandler = dialect.handlerForKind("nds.program");
  ItemTreeBuilder items(asset.metadata.items, input.ids);
  const auto root = items.add(std::nullopt, ItemKind::Sequence, "sseq", name,
                              input.reader.range(sequenceOffset, range.sequenceEnd - sequenceOffset));

  u32 trackIndex = 0;
  for (const u32 start : ndsSequenceTrackStarts(input.reader, sequenceOffset, range.sequenceEnd)) {
    auto track = decodeNdsSequenceTrack(input.reader, dialect, sequenceOffset, range.sequenceEnd, start, trackIndex++,
                                        range.recoverMalformedSdatRange);
    const auto trackItem = items.add(root, ItemKind::Track, "track", fmt::format("Track {}", track.sourceTrackNumber),
                                     input.reader.range(start, 0));
    for (const auto& command : track.commands) {
      static_cast<void>(addSourceCommandItem(items, trackItem, dialect, track, command));
      if (programHandler != nullptr) {
        addBankedProgramReference(asset.program, track, command, programHandler->kind, "raw", instrumentSet);
      }
    }
    asset.program.tracks.push_back(std::move(track));
  }

  return asset;
}

}  // namespace vgmtrans::formats::nds
