/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequence.h"

#include "value/base/LevelScale.h"
#include "value/sequence/SequenceCursorDialect.h"
#include "value/sequence/SequenceVm.h"
#include "value/sequence/bytecode/BytecodeWalkers.h"

#include <fmt/format.h>

#include <algorithm>
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
constexpr u32 kSseqFileSizeOffset = 0x08;
constexpr u32 kSseqDataOffsetField = 0x18;
constexpr u32 kSseqHeaderSize = 0x1c;

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
      : sequenceDataBase(context.sequenceOffset + kSseqHeaderSize), sequenceEnd(context.sequenceEnd) {}

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
         sequenceOffset + kSseqHeaderSize;
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

  const u32 trackStart = offset + kSseqHeaderSize;
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

struct PreservedCommandSpec {
  u8 opcode;
  std::string_view name;
  size_t operandBytes = 0;
  std::string_view kind = {};
};

constexpr auto kPreservedCommands = std::to_array<PreservedCommandSpec>({
    {0x93, "Open Track", 4},
    {0xa0, "Cmd with Random Value", 5, "random-value"},
    {0xa1, "Cmd with Variable", 2, "variable-command"},
    {0xa2, "If"},
    {0xb0, "Set Variable", 3},
    {0xb1, "Add Variable", 3},
    {0xb2, "Sub Variable", 3},
    {0xb3, "Mul Variable", 3},
    {0xb4, "Div Variable", 3},
    {0xb5, "Shift Variable", 3},
    {0xb6, "Rand Variable", 3},
    {0xb8, "If Variable ==", 3, "if-variable-equal"},
    {0xb9, "If Variable >=", 3, "if-variable-greater-equal"},
    {0xba, "If Variable >", 3, "if-variable-greater"},
    {0xbb, "If Variable <=", 3, "if-variable-less-equal"},
    {0xbc, "If Variable <", 3, "if-variable-less"},
    {0xbd, "If Variable !=", 3, "if-variable-not-equal"},
    {0xc2, "Master Volume", 1},
    {0xc6, "Priority", 1},
    {0xc8, "Tie", 1},
    {0xc9, "Portamento Control", 1},
    {0xcb, "Modulation Speed", 1},
    {0xcc, "Modulation Type", 1},
    {0xcd, "Modulation Range", 1},
    {0xd0, "Attack Rate", 1},
    {0xd1, "Decay Rate", 1},
    {0xd2, "Sustain Level", 1},
    {0xd3, "Release Rate", 1},
    {0xd4, "Loop Start", 1},
    {0xd6, "Print Variable", 1},
    {0xe0, "Modulation Delay", 2},
    {0xe3, "Sweep Pitch", 2},
    {0xfc, "Loop End"},
    {0xfe, "Allocate Track", 2},
});

[[nodiscard]] const PreservedCommandSpec* preservedCommand(u8 opcode) {
  const auto found = std::ranges::find_if(kPreservedCommands,
                                          [opcode](const PreservedCommandSpec& spec) { return spec.opcode == opcode; });
  return found == kPreservedCommands.end() ? nullptr : &*found;
}

struct NdsCommandReader {
  template <class Runtime>
  static CommandFlow read(Runtime& rt, VmCommandCursor& cmd) {
    const u8 opcode = cmd.opcode();
    if (opcode <= 0x7f) {
      cmd.name("Note")
          .semantic(SequenceSemantic::Note)
          .derived("key", static_cast<u64>(opcode), SourceValueDisplay::MidiNote);
      const u8 velocity = cmd.u8("velocity");
      const u32 duration = cmd.varLen("duration");
      rt.note(static_cast<double>(std::clamp<s32>(static_cast<s32>(opcode) + rt.state.transpose, 0, 127)),
              LevelScale::linearFromMidi7(velocity), duration);
      return rt.state.noteWait ? cmd.wait(duration) : cmd.next();
    }

    if (const auto* preserved = preservedCommand(opcode)) {
      return preserve(cmd, preserved->name, preserved->operandBytes, preserved->kind);
    }

    switch (opcode) {
      case 0x80:
        cmd.name("Rest").semantic(SequenceSemantic::Rest);
        return cmd.wait(cmd.varLen("duration"));

      case 0x81: {
        cmd.name("Program").semantic(SequenceSemantic::Program);
        const u32 raw = cmd.varLen("raw");
        const u32 bank = raw >> 7;
        const u32 program = raw & 0x7f;
        cmd.derived("bank", static_cast<u64>(bank))
            .derived("program", static_cast<u64>(program))
            .instrumentRef(bank, program);
        rt.instrument(bank, program);
        return cmd.next();
      }

      case 0x94: {
        cmd.name("Jump").semantic(SequenceSemantic::Jump);
        const Address destination =
            cmd.le24RelativeAddress("destination", Address{static_cast<u32>(rt.state.sequenceDataBase)});
        if (decodeTargetOutsideSequence(cmd, rt, destination)) {
          return cmd.end();
        }
        return cmd.jump(destination);
      }

      case 0x95: {
        cmd.name("Call").semantic(SequenceSemantic::Call);
        const Address destination =
            cmd.le24RelativeAddress("destination", Address{static_cast<u32>(rt.state.sequenceDataBase)});
        if (decodeTargetOutsideSequence(cmd, rt, destination)) {
          return cmd.end();
        }
        return cmd.call(destination);
      }

      case 0x96:
        return unsupported(cmd, rt, "Unsupported NDS SSEQ command stopped playback");

      case 0xc0: {
        cmd.name("Pan").semantic(SequenceSemantic::Pan);
        const u8 raw = cmd.u8("pan");
        rt.pan(std::clamp((static_cast<double>(raw) / 63.5) - 1.0, -1.0, 1.0));
        return cmd.next();
      }

      case 0xc1:
        cmd.name("Volume").semantic(SequenceSemantic::Level);
        rt.level(LevelScale::linearFromMidi7(cmd.u8("volume")));
        return cmd.next();

      case 0xc3:
        cmd.name("Transpose").semantic(SequenceSemantic::State);
        rt.state.transpose = cmd.s8("semitones");
        return cmd.next();

      case 0xc4: {
        cmd.name("Pitch Bend").semantic(SequenceSemantic::Pitch);
        const s8 bend = cmd.s8("bend");
        rt.pitchBend((static_cast<double>(bend) / 128.0) * rt.state.pitchBendRangeSemitones);
        return cmd.next();
      }

      case 0xc5: {
        cmd.name("Pitch Bend Range").semantic(SequenceSemantic::Pitch);
        const u8 semitones = cmd.u8("semitones");
        rt.state.pitchBendRangeSemitones = semitones;
        rt.pitchBendRange(semitones);
        return cmd.next();
      }

      case 0xc7:
        cmd.name("Note Wait").semantic(SequenceSemantic::State);
        rt.state.noteWait = cmd.u8("enabled") != 0;
        return cmd.next();

      case 0xca: {
        cmd.name("Modulation Depth").semantic(SequenceSemantic::Modulation);
        const u8 depth = cmd.u8("depth");
        rt.modulation(ModulationPerformanceTarget::VibratoDepth,
                      std::clamp(static_cast<double>(depth) / 127.0, 0.0, 1.0));
        return cmd.next();
      }

      case 0xce:
        cmd.name("Portamento").semantic(SequenceSemantic::Portamento);
        rt.portamentoEnable(cmd.u8("enabled") != 0);
        return cmd.next();

      case 0xcf:
        cmd.name("Portamento Time").semantic(SequenceSemantic::Portamento);
        rt.portamentoTime(static_cast<double>(cmd.u8("time")));
        return cmd.next();

      case 0xd5:
        cmd.name("Expression").kind("expression").semantic(SequenceSemantic::Level);
        rt.expression(LevelScale::linearFromMidi7(cmd.u8("expression")));
        return cmd.next();

      case 0xe1: {
        cmd.name("Tempo").semantic(SequenceSemantic::Tempo);
        const u16 bpm = cmd.u16le("bpm");
        if (bpm != 0) {
          rt.tempo(static_cast<u32>(std::round(60000000.0 / bpm)));
        }
        return cmd.next();
      }

      case 0xfd:
        return cmd.name("Return")
            .kind("return")
            .semantic(SequenceSemantic::Return)
            .playbackStatus(CommandPlaybackStatus::AffectsControlFlow)
            .ret();

      case 0xff:
        return cmd.name("End")
            .kind("end")
            .semantic(SequenceSemantic::End)
            .playbackStatus(CommandPlaybackStatus::StopsPlayback)
            .end();

      default:
        return unsupported(cmd, rt, "Unknown NDS SSEQ opcode stopped playback", "Unknown Opcode", "unknown");
    }
  }

private:
  template <class Runtime>
  static CommandFlow unsupported(VmCommandCursor& cmd, Runtime& rt, std::string_view message,
                                 std::string_view name = "Unsupported Command", std::string_view kind = "unsupported") {
    cmd.name(name).kind(kind).semantic(SequenceSemantic::Unsupported).unsupported(message);
    renderWarning(rt, std::string(message));
    return cmd.end();
  }

  static CommandFlow preserve(VmCommandCursor& cmd, std::string_view name, size_t operandBytes = 0,
                              std::string_view kind = {}) {
    cmd.name(name).semantic(SequenceSemantic::Meta).sourceOnly();
    if (!kind.empty()) {
      cmd.kind(kind);
    }
    if (operandBytes > 0) {
      static_cast<void>(cmd.rawBytes("bytes", operandBytes));
    }
    return cmd.next();
  }
};

[[nodiscard]] TrackProgram makeTrack(u32 startOffset, u32 trackIndex) {
  return TrackProgram{
      .id = TrackId{trackIndex},
      .sourceTrackNumber = trackIndex,
      .startAddress = Address{startOffset},
  };
}

[[nodiscard]] DecodedBytecodeCommand terminalRecoveryCommand(const SequenceDialect& dialect, ByteReader reader,
                                                             u32 offset) {
  const auto bytes = reader.slice(offset, 1);
  std::vector<u8> ownedBytes{bytes.begin(), bytes.end()};
  auto command = DecodedBytecodeCommand{
      .handler = cursorDialectHandlerId<TrackState, Context, NdsCommandReader>(dialect),
      .commandKind =
          CommandKind{
              .kindName = dialect.commandKindPrefix + ".end",
              .name = "End",
              .detailKind = dialect.commandKindPrefix + ".end",
              .semantic = SequenceSemantic::End,
              .playbackStatus = CommandPlaybackStatus::StopsPlayback,
          },
      .range = reader.range(offset, 1),
      .bytes = std::move(ownedBytes),
  };
  command.flow = DecodeFlow::terminalFlow();
  return command;
}

// Recovery decoder for malformed SDAT ranges that do not contain a normal SSEQ
// header. Normal SSEQ decode stays source-driver oriented; this path repairs
// range-level damage before source commands can be trusted.
[[nodiscard]] TrackProgram decodeMalformedSdatRangeTrack(ByteReader reader, const SequenceDialect& dialect,
                                                         u32 sequenceOffset, u32 sequenceEnd, u32 startOffset,
                                                         u32 trackIndex, size_t maxCommands,
                                                         SourceMapBuilder* sourceMap,
                                                         std::vector<Diagnostic>* diagnostics) {
  TrackProgram track = makeTrack(startOffset, trackIndex);
  TrackProgramBuilder builder{track};
  u32 offset = startOffset;
  size_t decodedCommands = 0;
  std::set<u32> decodedOffsets;
  std::set<u32> callTargetOffsets;
  std::vector<PendingBlock> pendingBlocks{{.offset = startOffset}};
  BytecodeDecodeContext decodeContext{
      .bytecodeEnd = sequenceEnd,
      .sequenceOffset = sequenceOffset,
      .sequenceEnd = sequenceEnd,
      .sourceMap = sourceMap,
      .diagnostics = diagnostics,
  };
  TrackState decodeState = makeDecodeCursorState<TrackState, Context>(decodeContext, cursorContext<Context>(dialect));

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

      auto decoded = decodeCursorCommandWithState<TrackState, Context, NdsCommandReader>(reader, offset, dialect,
                                                                                         decodeState, decodeContext);

      if (!block.callTarget) {
        const auto overlap = std::ranges::find_if(
            callTargetOffsets, [&](u32 target) { return begin < target && target < decoded.range.endOffset(); });
        if (overlap != callTargetOffsets.end()) {
          // Some malformed FAT entries fall through one byte before a real call
          // target. Stop before consuming the overlapping subroutine bytes.
          appendDecodedBytecodeCommand(builder, terminalRecoveryCommand(dialect, reader, begin), begin);
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

// Normal SSEQ decode follows statically reachable bytecode blocks from the
// track start, preserving calls and jumps as source commands.
[[nodiscard]] TrackProgram decodeReachableBlocks(ByteReader reader, const SequenceDialect& dialect, u32 sequenceOffset,
                                                 u32 sequenceEnd, u32 startOffset, u32 trackIndex,
                                                 SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  BytecodeDecodeContext decodeContext{
      .bytecodeEnd = sequenceEnd,
      .sequenceOffset = sequenceOffset,
      .sequenceEnd = sequenceEnd,
      .sourceMap = sourceMap,
      .diagnostics = diagnostics,
  };
  TrackState decodeState = makeDecodeCursorState<TrackState, Context>(decodeContext, cursorContext<Context>(dialect));
  return decodeReachableBytecodeBlocks(
      reader, sequenceEnd, startOffset, trackIndex,
      ReachableBytecodeDecodePolicy{.maxCommands = static_cast<u32>(kMaxTrackCommands)}, [&](u32 offset) {
        return decodeCursorCommandWithState<TrackState, Context, NdsCommandReader>(reader, offset, dialect, decodeState,
                                                                                   decodeContext);
      });
}

[[nodiscard]] NdsSequenceDescriptor makeNdsSequenceDescriptor() {
  return NdsSequenceDescriptor{
      .dialect = makeCursorDialect<TrackState, Context, NdsCommandReader>(CursorDialectSpec<Context>{
          .id = std::string(kNdsSequenceDialectId),
          .commandKindPrefix = "nds",
          .timebase = Timebase{.ppqn = 0x30},
          .defaultBehavior =
              SequenceProgramBehavior{
                  .defaultLoopPolicy = LoopPolicy::PlayOnce,
                  .commandLimit = static_cast<u32>(kMaxTrackCommands),
              },
          .context = Context{},
      }),
  };
}

}  // namespace

const NdsSequenceDescriptor& ndsSequenceDescriptor() {
  static const NdsSequenceDescriptor descriptor = makeNdsSequenceDescriptor();
  return descriptor;
}

void registerNdsSequenceDialect(SequenceDialectRegistry& registry) {
  registry.add(ndsSequenceDescriptor().dialect);
}

TrackProgram decodeNdsSequenceTrack(ByteReader reader, const NdsSequenceDescriptor& descriptor, u32 sequenceOffset,
                                    u32 sequenceEnd, u32 startOffset, u32 trackIndex, bool recoverMalformedSdatRange,
                                    SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  if (recoverMalformedSdatRange) {
    return decodeMalformedSdatRangeTrack(reader, descriptor.dialect, sequenceOffset, sequenceEnd, startOffset,
                                         trackIndex, kMaxTrackCommands, sourceMap, diagnostics);
  }
  return decodeReachableBlocks(reader, descriptor.dialect, sequenceOffset, sequenceEnd, startOffset, trackIndex,
                               sourceMap, diagnostics);
}

std::vector<u32> ndsSequenceTrackStarts(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd) {
  std::vector<u32> extraStarts;
  u32 offset = sequenceOffset + kSseqHeaderSize;
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
    if (status == 0x80) {
      const u32 statusOffset = offset;
      ++offset;
      if (!readVarLen(reader, offset, sequenceEnd)) {
        return {statusOffset};
      }
      if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
        return {offset};
      }
      status = reader.u8At(offset);
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
  const u32 recoveredEnd = recoveredSequenceOffset && reader.has(*recoveredSequenceOffset + kSseqFileSizeOffset, 4)
                               ? static_cast<u32>(std::min<u64>(
                                     reader.size(), static_cast<u64>(*recoveredSequenceOffset) +
                                                        reader.le32(*recoveredSequenceOffset + kSseqFileSizeOffset)))
                               : static_cast<u32>(reader.size());
  const u32 emptySequenceEnd =
      static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + kSseqHeaderSize));
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
              .sourceBaseAddress = Address{sequenceOffset + kSseqHeaderSize},
              .behavior = dialect.defaultBehavior,
          },
  };

  ItemTreeBuilder items(asset.metadata.items, input.ids);
  const auto root = items.add(std::nullopt, ItemKind::Sequence, "sseq", name,
                              input.reader.range(sequenceOffset, range.sequenceEnd - sequenceOffset));
  if (sourceMap != nullptr && input.reader.has(sequenceOffset, kSseqHeaderSize)) {
    sourceMap->header("SSEQ Header", input.reader.range(sequenceOffset, kSseqHeaderSize))
        .kind("sseq-header")
        .field("data_offset", input.reader.range(sequenceOffset + kSseqDataOffsetField, 4),
               static_cast<u64>(sequenceOffset + input.reader.le32(sequenceOffset + kSseqDataOffsetField)),
               SourceValueDisplay::Address);
  }

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
