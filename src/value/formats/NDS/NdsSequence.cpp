/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequence.h"

#include "value/base/LevelScale.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 262144;
constexpr u32 kSseqDataOffsetField = 0x18;
constexpr u32 kSseqHeaderSize = 0x1c;

struct PendingBlock {
  u32 offset = 0;
  bool callTarget = false;
};

// Only registers that persist from one executed source command to the next
// belong here. Source bounds and relative-address bases are decode concerns.
struct TrackState {
  bool noteWait = false;
  s32 transpose = 0;
  u8 pitchBendRangeSemitones = 2;
};

// Only driver behavior that depends on runtime track history needs a method.
// Ordinary commands compile directly to VM actions in decodeCommand below.
struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;

  [[nodiscard]] Effects note(u8 sourceKey, u8 velocity, u32 duration) {
    const s32 key = std::clamp<s32>(static_cast<s32>(sourceKey) + track.transpose, 0, 127);
    out.note(static_cast<double>(key), LevelScale::linearFromMidi7(velocity), duration);
    return track.noteWait ? Effects::wait(duration) : Effects{};
  }
};

using NdsCompilerCursor = CompilerCursor<TrackState, Playback>;
using NdsCompiledDialect = CompiledCommandDialect<TrackState, Playback>;

[[nodiscard]] Address targetAddress(NdsCompilerCursor::Event& event, u32 sequenceDataBase, u32 sequenceEnd,
                                    SemanticOperandRole role) {
  const u32 relative = event.u24le("relative", SourceValueDisplay::Address);
  const Address destination{sequenceDataBase + relative};
  return event.derived("destination", destination, SourceValueDisplay::Address,
                       destination.value < sequenceEnd ? role : SemanticOperandRole::Address);
}

// One source opcode is read and compiled in one block. Event operations append
// shared actions or typed Playback behavior in written order; there is no
// second opcode profile or execution switch.
[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end, u32 sequenceDataBase,
                                                   u32 sequenceEnd, std::vector<Diagnostic>* diagnostics) {
  NdsCompilerCursor cursor(reader, begin, end, "nds", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  if (cursor.opcode() <= 0x7f) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 key =
        event.opcodeValue("key", cursor.opcode(), SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const u8 velocity = event.u8("velocity", SourceValueDisplay::Default, SemanticOperandRole::Level);
    const u32 duration = event.varLen("duration", SourceValueDisplay::Default, SemanticOperandRole::Duration);
    return event.invoke<&Playback::note>(key, velocity, duration);
  }

  switch (cursor.opcode()) {
    case 0x80: {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      return event.wait(event.varLen("duration", SourceValueDisplay::Default, SemanticOperandRole::Duration));
    }
    case 0x81: {
      auto event = cursor.command("Program", SequenceSemantic::Program);
      const u32 raw = event.varLen("raw");
      const u32 bank =
          event.derived("bank", raw >> 7, SourceValueDisplay::Default, SemanticOperandRole::InstrumentBank);
      const u32 program =
          event.derived("program", raw & 0x7f, SourceValueDisplay::Default, SemanticOperandRole::InstrumentProgram);
      return event.emitInstrument(bank, program);
    }
    case 0x93: {
      auto event = cursor.sourceOnly("Open Track");
      event.u8("track");
      static_cast<void>(targetAddress(event, sequenceDataBase, sequenceEnd, SemanticOperandRole::Address));
      return event.ignore();
    }
    case 0x94:
    case 0x95: {
      const bool isCall = cursor.opcode() == 0x95;
      auto event = cursor.command(isCall ? "Call" : "Jump", isCall ? SequenceSemantic::Call : SequenceSemantic::Jump,
                                  CommandPlaybackStatus::AffectsControlFlow);
      const Address destination =
          targetAddress(event, sequenceDataBase, sequenceEnd,
                        isCall ? SemanticOperandRole::CallTarget : SemanticOperandRole::JumpTarget);
      if (!event.ok()) {
        return event.stop();
      }
      if (destination.value >= sequenceEnd) {
        event.warning(isCall ? "Call target outside sequence data" : "Jump target outside sequence data");
        return event.stop();
      }
      return isCall ? event.call(destination) : event.jump(destination);
    }
    case 0x96: {
      auto event = cursor.unsupported("Unsupported Command");
      event.warning("Unsupported NDS SSEQ command stopped playback");
      return event.stop();
    }
    case 0xa0:
      return cursor.opaque("Cmd with Random Value", 5, "random-value");
    case 0xa1:
      return cursor.opaque("Cmd with Variable", 2, "variable-command");
    case 0xa2:
      return cursor.opaque("If", 0);
    case 0xb0:
      return cursor.opaque("Set Variable", 3);
    case 0xb1:
      return cursor.opaque("Add Variable", 3);
    case 0xb2:
      return cursor.opaque("Sub Variable", 3);
    case 0xb3:
      return cursor.opaque("Mul Variable", 3);
    case 0xb4:
      return cursor.opaque("Div Variable", 3);
    case 0xb5:
      return cursor.opaque("Shift Variable", 3);
    case 0xb6:
      return cursor.opaque("Rand Variable", 3);
    case 0xb8:
      return cursor.opaque("If Variable ==", 3, "if-variable-equal");
    case 0xb9:
      return cursor.opaque("If Variable >=", 3, "if-variable-greater-equal");
    case 0xba:
      return cursor.opaque("If Variable >", 3, "if-variable-greater");
    case 0xbb:
      return cursor.opaque("If Variable <=", 3, "if-variable-less-equal");
    case 0xbc:
      return cursor.opaque("If Variable <", 3, "if-variable-less");
    case 0xbd:
      return cursor.opaque("If Variable !=", 3, "if-variable-not-equal");
    case 0xc0: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const double position = std::clamp((event.u8("pan") / 63.5) - 1.0, -1.0, 1.0);
      return event.emitPan(position);
    }
    case 0xc1: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.emitLevel(LevelScale::linearFromMidi7(event.u8("volume")));
    }
    case 0xc2:
      return cursor.opaque("Master Volume", 1);
    case 0xc3: {
      auto event = cursor.command("Transpose", SequenceSemantic::State);
      return event.set<&TrackState::transpose>(event.s8("semitones"));
    }
    case 0xc4: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      return event.emitPitchBendScaledBy<&TrackState::pitchBendRangeSemitones>(event.s8("bend") / 128.0);
    }
    case 0xc5: {
      auto event = cursor.command("Pitch Bend Range", SequenceSemantic::Pitch);
      const u8 semitones = event.u8("semitones");
      return event.set<&TrackState::pitchBendRangeSemitones>(semitones).emitPitchBendRange(semitones);
    }
    case 0xc6:
      return cursor.opaque("Priority", 1);
    case 0xc7: {
      auto event = cursor.command("Note Wait", SequenceSemantic::State);
      return event.set<&TrackState::noteWait>(event.u8("enabled") != 0);
    }
    case 0xc8:
      return cursor.opaque("Tie", 1);
    case 0xc9:
      return cursor.opaque("Portamento Control", 1);
    case 0xca: {
      auto event = cursor.command("Modulation Depth", SequenceSemantic::Modulation);
      const double amount = std::clamp(event.u8("depth") / 127.0, 0.0, 1.0);
      return event.emitModulation(ModulationPerformanceTarget::VibratoDepth, amount);
    }
    case 0xcb:
      return cursor.opaque("Modulation Speed", 1);
    case 0xcc:
      return cursor.opaque("Modulation Type", 1);
    case 0xcd:
      return cursor.opaque("Modulation Range", 1);
    case 0xce: {
      auto event = cursor.command("Portamento", SequenceSemantic::Portamento);
      return event.emitPortamentoEnable(event.u8("enabled") != 0);
    }
    case 0xcf: {
      auto event = cursor.command("Portamento Time", SequenceSemantic::Portamento);
      return event.emitPortamentoTime(event.u8("time"));
    }
    case 0xd0:
      return cursor.opaque("Attack Rate", 1);
    case 0xd1:
      return cursor.opaque("Decay Rate", 1);
    case 0xd2:
      return cursor.opaque("Sustain Level", 1);
    case 0xd3:
      return cursor.opaque("Release Rate", 1);
    case 0xd4:
      return cursor.opaque("Loop Start", 1);
    case 0xd5: {
      auto event = cursor.command("Expression", SequenceSemantic::Level);
      return event.emitExpression(LevelScale::linearFromMidi7(event.u8("expression")));
    }
    case 0xd6:
      return cursor.opaque("Print Variable", 1);
    case 0xe0:
      return cursor.opaque("Modulation Delay", 2);
    case 0xe1: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u16 bpm = event.u16le("bpm");
      return bpm == 0 ? event.ignore() : event.emitTempo(static_cast<u32>(std::round(60000000.0 / bpm)));
    }
    case 0xe3:
      return cursor.opaque("Sweep Pitch", 2);
    case 0xfc:
      return cursor.opaque("Loop End", 0);
    case 0xfd:
      return cursor.command("Return", SequenceSemantic::Return, CommandPlaybackStatus::AffectsControlFlow).return_();
    case 0xfe: {
      auto event = cursor.sourceOnly("Allocate Track");
      event.u16le("track_mask");
      return event.ignore();
    }
    case 0xff:
      return cursor.command("End", SequenceSemantic::End).end();
    default: {
      auto event = cursor.unsupported("Unknown Opcode", "unknown");
      event.warning("Unknown NDS SSEQ opcode stopped playback");
      return event.stop();
    }
  }
}

[[nodiscard]] SequenceDialect makeDialect() {
  return SequenceDialect{
      .id = DialectId{.value = std::string(kNdsSequenceDialectId)},
      .commandDetailKindPrefix = "nds",
      .timebase = Timebase{.ppqn = 0x30},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .commandLimit = kMaxTrackCommands,
          },
      .createSemanticTrackState = NdsCompiledDialect::createTrackState,
      .executeSemantic = NdsCompiledDialect::execute,
  };
}

[[nodiscard]] DecodedBytecodeCommand terminalRecoveryCommand(ByteReader reader, u32 offset) {
  return DecodedBytecodeCommand{
      .range = reader.range(offset, 1),
      .opcode = reader.u8At(offset),
      .encodedSize = 1,
      .flow = DecodeFlow::terminalFlow(),
      .presentation =
          DecodedCommandPresentation{
              .label = "Recovery Stop",
              .localKind = "recovery-stop",
              .detailKind = "nds.recovery-stop",
              .semantic = SequenceSemantic::Unsupported,
              .playback = CommandPlaybackStatus::StopsPlayback,
          },
      .retainBytes = false,
  };
}

// Malformed SDAT FAT ranges can overlap a real call target by one byte. This
// exceptional walker keeps the normal compiler cursor, adding only
// the overlap stop needed to avoid swallowing the subroutine's first byte.
[[nodiscard]] TrackProgram decodeMalformedSdatRangeTrack(ByteReader reader, TrackDecodeInput input) {
  TrackProgram track{
      .id = TrackId{input.trackIndex},
      .sourceTrackNumber = input.trackIndex,
      .startAddress = Address{input.startOffset},
  };
  TrackProgramBuilder builder{track};
  const auto trackAnnotation = createSequenceTrackAnnotation(reader, input);
  std::set<u32> decodedOffsets;
  std::set<u32> callTargetOffsets;
  std::vector<PendingBlock> pendingBlocks{{.offset = input.startOffset}};
  u32 decodedCommands = 0;
  const u32 sequenceDataBase = input.sequenceOffset + kSseqHeaderSize;

  const auto decodeAt = [&](u32 offset) {
    auto decoded =
        decodeCommand(reader, offset, input.bytecodeEnd, sequenceDataBase, input.sequenceEnd, input.diagnostics);
    decoded.annotation = projectDecodedCommand(input.sourceMap, decoded, trackAnnotation);
    return decoded;
  };

  while (!pendingBlocks.empty()) {
    const PendingBlock block = pendingBlocks.back();
    pendingBlocks.pop_back();
    u32 offset = block.offset;

    while (hasBytecodeBytes(reader, offset, 1, input.bytecodeEnd) && decodedCommands++ < input.maxCommands) {
      const u32 begin = offset;
      if (!decodedOffsets.insert(begin).second) {
        break;
      }

      auto decoded = decodeAt(begin);
      if (!block.callTarget) {
        const auto overlap = std::ranges::find_if(
            callTargetOffsets, [&](u32 target) { return begin < target && target < decoded.range.endOffset(); });
        if (overlap != callTargetOffsets.end()) {
          auto recovery = terminalRecoveryCommand(reader, begin);
          recovery.annotation = projectDecodedCommand(input.sourceMap, recovery, trackAnnotation);
          appendDecodedBytecodeCommand(builder, recovery, begin);
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

  finishSequenceTrackAnnotation(reader, input, trackAnnotation, track);
  return track;
}

}  // namespace

const SequenceDialect& ndsSequenceDialect() {
  static const SequenceDialect dialect = makeDialect();
  return dialect;
}

TrackProgram decodeNdsSequenceTrack(ByteReader reader, TrackDecodeInput input, bool recoverMalformedSdatRange) {
  if (input.bytecodeEnd == std::numeric_limits<u32>::max()) {
    input.bytecodeEnd = static_cast<u32>(reader.size());
  } else {
    input.bytecodeEnd = std::min(input.bytecodeEnd, static_cast<u32>(reader.size()));
  }
  if (input.sequenceEnd == std::numeric_limits<u32>::max()) {
    input.sequenceEnd = input.bytecodeEnd;
  }
  if (recoverMalformedSdatRange) {
    return decodeMalformedSdatRangeTrack(reader, input);
  }

  const u32 sequenceDataBase = input.sequenceOffset + kSseqHeaderSize;
  return decodeCompilerReachableTrack(reader, input, [reader, input, sequenceDataBase](u32 offset) {
    return decodeCommand(reader, offset, input.bytecodeEnd, sequenceDataBase, input.sequenceEnd, input.diagnostics);
  });
}

std::vector<u32> ndsSequenceTrackAddresses(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd) {
  std::vector<u32> extraTrackAddresses;
  const u32 sequenceDataBase = sequenceOffset + kSseqHeaderSize;
  u32 offset = sequenceDataBase;
  if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
    return {offset};
  }

  // Bootstrap commands use the same semantic decoder as normal tracks. They
  // are discarded after discovery because the driver runs them before musical
  // track playback; the primary track begins at the first following command.
  const auto decodeAt = [&](u32 commandOffset) {
    return decodeCommand(reader, commandOffset, sequenceEnd, sequenceDataBase, sequenceEnd, nullptr);
  };

  auto command = decodeAt(offset);
  if (command.opcode != 0xfe || command.retainBytes) {
    return {offset};
  }
  offset += command.encodedSize;
  if (!hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
    return {offset};
  }

  command = decodeAt(offset);
  if (command.opcode == 0x80) {
    if (command.retainBytes) {
      return {offset};
    }
    offset += command.encodedSize;
  }

  while (hasBytecodeBytes(reader, offset, 1, sequenceEnd)) {
    command = decodeAt(offset);
    if (command.opcode != 0x93 || command.retainBytes) {
      break;
    }
    const auto destination = std::ranges::find_if(
        command.operands, [](const SemanticOperand& operand) { return operand.name == "destination"; });
    if (destination != command.operands.end()) {
      if (const auto* address = std::get_if<Address>(&destination->value);
          address != nullptr && address->value < sequenceEnd) {
        extraTrackAddresses.push_back(address->value);
      }
    }
    offset += command.encodedSize;
  }

  std::vector<u32> trackAddresses{offset};
  trackAddresses.insert(trackAddresses.end(), extraTrackAddresses.begin(), extraTrackAddresses.end());
  return trackAddresses;
}

SequenceProgramAsset parseNdsSequenceProgram(const ScanInput& input, AssetId id, NdsSequenceRange range,
                                             const std::string& name, SourceMapBuilder* sourceMap,
                                             std::vector<Diagnostic>* diagnostics) {
  const SequenceDialect& dialect = ndsSequenceDialect();
  const u32 sequenceOffset = range.offset;
  const SourceRange sequenceRange = input.reader.range(sequenceOffset, range.sequenceEnd - sequenceOffset);
  SequenceProgramAsset asset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kNdsFormatName),
              .name = name,
              .range = sequenceRange,
          },
      .program =
          SequenceProgram{
              .dialect = dialect.id,
              .timebase = dialect.timebase,
              .sourceBaseAddress = Address{sequenceOffset + kSseqHeaderSize},
              .behavior = dialect.defaultBehavior,
          },
  };

  SourceAnnotationId headerAnnotation;
  if (sourceMap != nullptr && input.reader.has(sequenceOffset, kSseqHeaderSize)) {
    headerAnnotation = sourceMap->header("SSEQ Header", input.reader.range(sequenceOffset, kSseqHeaderSize))
                           .kind("sseq-header")
                           .owner(ObjectRefs::sequence(id))
                           .field("data_offset", input.reader.range(sequenceOffset + kSseqDataOffsetField, 4),
                                  sequenceOffset + input.reader.le32(sequenceOffset + kSseqDataOffsetField),
                                  SourceValueDisplay::Address)
                           .id();
  }

  u32 trackIndex = 0;
  for (const u32 trackAddress : ndsSequenceTrackAddresses(input.reader, sequenceOffset, range.sequenceEnd)) {
    asset.program.tracks.push_back(decodeNdsSequenceTrack(
        input.reader,
        TrackDecodeInput{
            .sequenceAsset = id,
            .trackIndex = trackIndex++,
            .startOffset = trackAddress,
            .bytecodeEnd = range.sequenceEnd,
            .sequenceOffset = sequenceOffset,
            .sequenceEnd = range.sequenceEnd,
            .parentAnnotation = headerAnnotation.valid() ? std::optional{headerAnnotation} : std::nullopt,
            .sourceMap = sourceMap,
            .diagnostics = diagnostics,
            .maxCommands = kMaxTrackCommands,
        },
        range.recoverMalformedSdatRange));
  }

  return asset;
}

}  // namespace vgmtrans::formats::nds
