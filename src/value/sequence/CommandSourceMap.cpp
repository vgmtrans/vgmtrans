/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/CommandSourceMap.h"

#include "value/model/SourceMap.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <algorithm>
#include <optional>
#include <string>

namespace vgmtrans::core {

namespace {

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

[[nodiscard]] std::optional<u32> sourceChannel(const DecodedBytecodeCommand& command) {
  const auto found = std::ranges::find_if(
      command.operands, [](const SemanticOperand& operand) { return operand.role == SemanticOperandRole::Channel; });
  return found == command.operands.end() ? std::nullopt : found->unsigned32();
}

[[nodiscard]] SourceAnnotationId projectDecodedCommand(SourceMapBuilder* sourceMap,
                                                       const DecodedBytecodeCommand& command,
                                                       std::optional<SourceAnnotationId> parent) {
  if (sourceMap == nullptr || !command.range.valid()) {
    return {};
  }

  auto annotation =
      sourceMap->command(command.presentation.label, command.range, command.presentation.semantic)
          .kind(command.presentation.kind)
          .field("opcode", SourceRange{.source = command.range.source, .offset = command.range.offset, .size = 1},
                 command.opcode, SourceValueDisplay::Hex)
          .fields(command.fields);
  if (parent) {
    annotation.parent(*parent);
  }

  std::optional<u32> instrumentBank;
  std::optional<u32> instrumentProgram;
  for (const auto& operand : command.operands) {
    if (const auto role = linkRole(operand.role)) {
      if (operand.value) {
        annotation.link(
            *role, SourceTarget{SourceRange{.source = command.range.source, .offset = *operand.value, .size = 1}});
      }
    }
    if (operand.role == SemanticOperandRole::InstrumentBank) {
      instrumentBank = operand.unsigned32();
    } else if (operand.role == SemanticOperandRole::InstrumentProgram) {
      instrumentProgram = operand.unsigned32();
    } else if (operand.role == SemanticOperandRole::Instrument) {
      if (const auto instrument = operand.unsigned32()) {
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

}  // namespace

TrackDecodeSession::TrackDecodeSession(const TrackDecodeScope& scope, u32 trackIndex, u64 startOffset)
    : startRange_(scope.reader.range(startOffset, 0)), sourceMap_(scope.sourceMap), trackIndex_(trackIndex) {
  if (!scope.sourceHasTracks) {
    commandParent_ = scope.parentAnnotation;
    if (!commandParent_) {
      rootSequenceAsset_ = scope.sequenceAsset;
    }
    return;
  }
  if (sourceMap_ == nullptr) {
    return;
  }

  auto track = sourceMap_->annotation(SourceRole::SequenceTrack, "Track " + std::to_string(trackIndex),
                                      startRange_)
                   .kind("track");
  if (scope.sequenceAsset) {
    track.owner(ObjectRefs::sequenceTrack(*scope.sequenceAsset, trackIndex));
  }
  if (scope.parentAnnotation) {
    track.parent(*scope.parentAnnotation);
  }
  annotation_ = track.id();
  commandParent_ = annotation_;
}

TrackProgram TrackDecodeSession::finish() {
  TrackProgram track{
      .startAddress = Address{startRange_.offset},
      .annotation = annotation_,
  };
  track.streams.front().channels.front() = trackIndex_;
  track.commands.reserve(commands_.size());
  SourceRange span;
  for (auto& [offset, decoded] : commands_) {
    span.include(decoded.range);
    const SourceAnnotationId annotation = projectDecodedCommand(sourceMap_, decoded, commandParent_);
    if (sourceMap_ != nullptr && annotation.valid() && rootSequenceAsset_) {
      AnnotationBuilder{*sourceMap_, annotation}.owner(ObjectRefs::sequence(*rootSequenceAsset_));
    }
    track.commands.push_back(SourceCommand{
        .opcode = decoded.opcode,
        .address = Address{offset},
        .range = decoded.range,
        .annotation = annotation,
        .semantic = decoded.presentation.semantic,
        .sourceChannel = sourceChannel(decoded),
        .flow = std::move(decoded.flow),
        .execution = std::move(decoded.execution),
    });
  }
  if (sourceMap_ != nullptr && annotation_.valid()) {
    AnnotationBuilder{*sourceMap_, annotation_}.range(span.valid() ? span : startRange_);
  }
  return track;
}

SequenceDecodeSession::SequenceDecodeSession(ByteReader reader, const SequenceProgramConfig& config,
                                             AssetId sequenceAsset, SourceRange headerRange,
                                             SourceMapBuilder* sourceMap, u32 maxTrackCommands, u32 bytecodeEnd)
    : tracks_{
          .reader = reader,
          .bytecodeEnd = bytecodeEnd,
          .maxCommands = maxTrackCommands,
          .sequenceAsset = sequenceAsset,
          .sourceMap = sourceMap,
      },
      program_(config.makeProgram()), sourceKindPrefix_(config.commandKindPrefix) {
  if (tracks_.sourceMap == nullptr) {
    return;
  }

  header_ = tracks_.sourceMap->header("Sequence Header", headerRange)
                .kind(sourceKindPrefix_ + "-sequence-header")
                .owner(ObjectRefs::sequence(sequenceAsset));
}

AnnotationBuilder SequenceDecodeSession::trackPointer(u32 trackIndex, SourceRange pointerRange, u32 startOffset,
                                                      std::optional<u64> encodedStartOffset) {
  if (tracks_.sourceMap == nullptr) {
    return {};
  }

  auto pointer =
      tracks_.sourceMap->pointer("Track Pointer", pointerRange, SourceTarget{tracks_.reader.range(startOffset, 1)})
          .kind(sourceKindPrefix_ + "-track-pointer")
          .owner(ObjectRefs::sequenceTrack(*tracks_.sequenceAsset, trackIndex));
  if (encodedStartOffset) {
    pointer.field("stored_destination", pointerRange, *encodedStartOffset, SourceValueDisplay::Address)
        .derived("destination", startOffset, SourceValueDisplay::Address);
  } else {
    pointer.field("destination", pointerRange, startOffset, SourceValueDisplay::Address);
  }
  return pointer.parent(header_.id());
}

}  // namespace vgmtrans::core
