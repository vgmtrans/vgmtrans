/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/CommandSourceMap.h"

#include "value/model/SourceMap.h"
#include "value/sequence/SequenceDialect.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::optional<SourceAnnotationId> createTrackAnnotation(
    ByteReader reader, u32 trackIndex, u32 startOffset, std::optional<AssetId> sequenceAsset,
    std::optional<SourceAnnotationId> parentAnnotation, SourceMapBuilder* sourceMap) {
  if (sourceMap == nullptr) {
    return std::nullopt;
  }

  auto track =
      sourceMap
          ->annotation(SourceRole::SequenceTrack, "Track " + std::to_string(trackIndex), reader.range(startOffset, 0))
          .kind("track");
  if (sequenceAsset) {
    track.owner(ObjectRefs::sequenceTrack(*sequenceAsset, trackIndex));
  }
  if (parentAnnotation) {
    track.parent(*parentAnnotation);
  }
  return track.id();
}

void finishTrackAnnotation(ByteReader reader, u32 startOffset, SourceMapBuilder* sourceMap,
                           std::optional<SourceAnnotationId> annotation, const TrackProgram& track) {
  if (sourceMap == nullptr || !annotation) {
    return;
  }

  std::optional<SourceRange> span;
  for (const SourceCommand& command : track.commands) {
    if (!command.range.valid() || (span && command.range.source != span->source)) {
      continue;
    }
    if (!span) {
      span = command.range;
      continue;
    }
    const u64 begin = std::min(span->offset, command.range.offset);
    const u64 end = std::max(span->endOffset(), command.range.endOffset());
    *span = SourceRange{.source = span->source, .offset = begin, .size = end - begin};
  }
  AnnotationBuilder{*sourceMap, *annotation}.range(span.value_or(reader.range(startOffset, 0)));
}

[[nodiscard]] std::optional<Address> operandAddress(const SemanticOperand& operand) {
  if (const auto* address = std::get_if<Address>(&operand.value)) {
    return *address;
  }
  if (const auto* value = std::get_if<u64>(&operand.value)) {
    return Address{*value};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<u32> operandUnsigned32(const SemanticOperand& operand) {
  const auto* value = std::get_if<u64>(&operand.value);
  if (value == nullptr || *value > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }
  return static_cast<u32>(*value);
}

[[nodiscard]] std::optional<SourceLinkRole> linkRole(SemanticOperandRole role) {
  switch (role) {
    case SemanticOperandRole::InstrumentTablePointer:
      return SourceLinkRole::PointsTo;
    case SemanticOperandRole::JumpTarget:
      return SourceLinkRole::JumpTarget;
    case SemanticOperandRole::CallTarget:
      return SourceLinkRole::CallTarget;
    case SemanticOperandRole::LoopTarget:
      return SourceLinkRole::LoopTarget;
    case SemanticOperandRole::RepeatTarget:
      return SourceLinkRole::RepeatTarget;
    default:
      return std::nullopt;
  }
}

void projectOperand(AnnotationBuilder& annotation, const SemanticOperand& operand) {
  if (operand.name.empty()) {
    return;
  }

  if (operand.encodedValue) {
    const std::string_view encodedName =
        operand.encodedName.empty() ? std::string_view{operand.name} : std::string_view{operand.encodedName};
    if (operand.range.valid()) {
      annotation.field(encodedName, operand.range, semanticOperandSourceValue(*operand.encodedValue),
                       operand.encodedDisplay);
    }
    annotation.derived(operand.name, semanticOperandSourceValue(operand.value), operand.display);
    return;
  }

  if (operand.range.valid()) {
    annotation.field(operand.name, operand.range, semanticOperandSourceValue(operand.value), operand.display);
  } else {
    annotation.derived(operand.name, semanticOperandSourceValue(operand.value), operand.display);
  }
}

}  // namespace

SourceAnnotationId projectDecodedCommand(SourceMapBuilder* sourceMap, const DecodedBytecodeCommand& command,
                                         std::optional<SourceAnnotationId> parent) {
  if (sourceMap == nullptr || !command.range.valid()) {
    return {};
  }

  auto annotation =
      sourceMap->command(command.presentation.label, command.range, command.presentation.semantic)
          .kind(command.presentation.localKind)
          .detailKind(command.presentation.detailKind)
          .playbackStatus(command.presentation.playback)
          .field("opcode", SourceRange{.source = command.range.source, .offset = command.range.offset, .size = 1},
                 command.opcode, SourceValueDisplay::Hex);
  if (parent) {
    annotation.parent(*parent);
  }

  std::optional<u32> instrumentBank;
  std::optional<u32> instrumentProgram;
  for (const auto& operand : command.operands) {
    projectOperand(annotation, operand);

    if (const auto role = linkRole(operand.role)) {
      if (const auto destination = operandAddress(operand)) {
        annotation.link(
            *role, SourceTarget{SourceRange{.source = command.range.source, .offset = destination->value, .size = 1}});
      }
    }
    if (operand.role == SemanticOperandRole::InstrumentBank) {
      instrumentBank = operandUnsigned32(operand);
    } else if (operand.role == SemanticOperandRole::InstrumentProgram) {
      instrumentProgram = operandUnsigned32(operand);
    } else if (operand.role == SemanticOperandRole::Instrument) {
      if (const auto instrument = operandUnsigned32(operand)) {
        annotation.link(SourceLinkRole::UsesInstrument, SourceTarget{ObjectRefs::instrumentIndex(*instrument)},
                        "Instrument");
      }
    }
  }

  if (instrumentBank && instrumentProgram) {
    annotation.link(SourceLinkRole::UsesInstrument,
                    SourceTarget{ObjectRefs::instrumentProgram(*instrumentBank, *instrumentProgram)}, "Instrument");
  }
  return annotation.id();
}

TrackDecodeSession::TrackDecodeSession(ByteReader reader, u32 trackIndex, u32 startOffset,
                                       std::optional<AssetId> sequenceAsset,
                                       std::optional<SourceAnnotationId> parentAnnotation, SourceMapBuilder* sourceMap)
    : reader_(reader), startOffset_(startOffset), sourceMap_(sourceMap),
      annotation_(createTrackAnnotation(reader, trackIndex, startOffset, sequenceAsset, parentAnnotation, sourceMap)),
      track_{
          .id = TrackId{trackIndex},
          .sourceTrackNumber = trackIndex,
          .startAddress = Address{startOffset},
      } {
}

DecodedBytecodeCommand TrackDecodeSession::project(DecodedBytecodeCommand command) const {
  command.annotation = projectDecodedCommand(sourceMap_, command, annotation_);
  return command;
}

void TrackDecodeSession::append(DecodedBytecodeCommand command, u32 offset) {
  command = project(std::move(command));
  TrackProgramBuilder builder{track_};
  appendDecodedBytecodeCommand(builder, command, offset);
}

TrackProgram TrackDecodeSession::finish() {
  finishTrackAnnotation(reader_, startOffset_, sourceMap_, annotation_, track_);
  return std::move(track_);
}

TrackProgram TrackDecodeSession::finish(TrackProgram track) {
  track_ = std::move(track);
  return finish();
}

SequenceDecodeSession::SequenceDecodeSession(ByteReader reader, const SequenceDialect& dialect,
                                             AssetId sequenceAsset, SourceRange headerRange,
                                             SourceMapBuilder* sourceMap, u32 maxTrackCommands)
    : tracks_{
          .reader = reader,
          .maxCommands = maxTrackCommands,
          .sequenceAsset = sequenceAsset,
          .sourceMap = sourceMap,
      },
      program_(dialect.makeProgram()), sourceKindPrefix_(dialect.commandDetailKindPrefix) {
  if (tracks_.sourceMap == nullptr) {
    return;
  }

  tracks_.parentAnnotation = tracks_.sourceMap->header("Sequence Header", headerRange)
                                 .kind(sourceKindPrefix_ + "-sequence-header")
                                 .owner(ObjectRefs::sequence(sequenceAsset))
                                 .id();
}

void SequenceDecodeSession::annotateTrackPointer(u32 trackIndex, SourceRange pointerRange, u32 startOffset) {
  if (tracks_.sourceMap == nullptr) {
    return;
  }

  tracks_.sourceMap->pointer("Track Pointer", pointerRange, SourceTarget{tracks_.reader.range(startOffset, 1)})
      .kind(sourceKindPrefix_ + "-track-pointer")
      .owner(ObjectRefs::sequenceTrack(*tracks_.sequenceAsset, trackIndex))
      .field("destination", pointerRange, startOffset, SourceValueDisplay::Address)
      .parent(*tracks_.parentAnnotation);
}

TrackDecodeScope makeTrackDecodeScope(ByteReader reader, const TrackDecodeInput& input) {
  return TrackDecodeScope{
      .reader = reader,
      .bytecodeEnd = input.bytecodeEnd,
      .maxCommands = input.maxCommands,
      .sequenceAsset = input.sequenceAsset,
      .parentAnnotation = input.parentAnnotation,
      .sourceMap = input.sourceMap,
  };
}

}  // namespace vgmtrans::core
