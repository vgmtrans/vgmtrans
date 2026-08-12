/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceProgram.h"

#include "value/base/Source.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

void AddressIndex::add(Address address, u32 commandIndex) {
  const auto [_, inserted] = commandByAddress.emplace(address.value, commandIndex);
  if (!inserted) {
    throw std::invalid_argument("Sequence command address was decoded more than once");
  }
}

std::optional<u32> AddressIndex::find(Address address) const {
  const auto found = commandByAddress.find(address.value);
  if (found == commandByAddress.end()) {
    return std::nullopt;
  }
  return found->second;
}

const TrackProgram* trackById(const SequenceProgram& program, TrackId id) {
  if (id.valid() && id.value < program.tracks.size()) {
    const auto& track = program.tracks[id.value];
    if (track.id == id) {
      return &track;
    }
  }

  const auto found = std::ranges::find_if(program.tracks, [id](const TrackProgram& track) { return track.id == id; });
  if (found == program.tracks.end()) {
    return nullptr;
  }
  return &*found;
}

const SourceCommand* sourceCommandById(const TrackProgram& track, CommandId id) {
  if (id.valid() && id.value < track.commands.size()) {
    const auto& command = track.commands[id.value];
    if (command.id == id) {
      return &command;
    }
  }

  const auto found =
      std::ranges::find_if(track.commands, [id](const SourceCommand& command) { return command.id == id; });
  if (found == track.commands.end()) {
    return nullptr;
  }
  return &*found;
}

bool trackUsesSemantic(const TrackProgram& track, SequenceSemantic semantic) {
  return std::ranges::any_of(track.commands,
                             [semantic](const SourceCommand& command) { return command.semantic == semantic; });
}

bool sequenceUsesSemantic(const SequenceProgram& program, SequenceSemantic semantic) {
  return std::ranges::any_of(program.tracks,
                             [semantic](const TrackProgram& track) { return trackUsesSemantic(track, semantic); });
}

SourceRange sequenceSourceRange(ByteReader reader, SourceRange baseRange, const SequenceProgram& program) {
  u64 first = baseRange.offset;
  u64 last = baseRange.endOffset();
  for (const TrackProgram& track : program.tracks) {
    for (const SourceCommand& command : track.commands) {
      if (command.range.valid() && command.range.source == baseRange.source) {
        first = std::min(first, command.range.offset);
        last = std::max(last, command.range.endOffset());
      }
    }
  }
  return reader.range(first, last - first);
}

const SemanticOperand* semanticOperand(const SourceCommand& command, std::string_view name) {
  const auto found =
      std::ranges::find_if(command.operands, [name](const SemanticOperand& operand) { return operand.name == name; });
  return found != command.operands.end() ? &*found : nullptr;
}

SourceValue semanticOperandSourceValue(const SemanticOperandValue& value) {
  return std::visit(
      [](const auto& typedValue) -> SourceValue {
        using T = std::decay_t<decltype(typedValue)>;
        if constexpr (std::is_same_v<T, Address>) {
          return makeSourceValue(typedValue.value);
        } else {
          return makeSourceValue(typedValue);
        }
      },
      value);
}

TrackProgramBuilder::TrackProgramBuilder(TrackProgram& track) : track_(track) {
}

const SourceCommand& TrackProgramBuilder::addSemantic(Address address, u8 opcode, u32 encodedSize, SourceRange range,
                                                      std::vector<SemanticOperand> operands, CommandFlow flow,
                                                      SourceAnnotationId annotation, CommandExecution execution,
                                                      SequenceSemantic semantic) {
  if (encodedSize == 0) {
    throw std::invalid_argument("Semantic sequence commands must include an opcode byte");
  }
  if (track_.addressIndex.find(address)) {
    throw std::invalid_argument("Sequence command address was decoded more than once");
  }

  const auto commandIndex = static_cast<u32>(track_.commands.size());
  track_.commands.push_back(SourceCommand{
      .id = CommandId{commandIndex},
      .opcode = opcode,
      .address = address,
      .encodedSize = encodedSize,
      .range = range,
      .annotation = annotation,
      .semantic = semantic,
      .operands = std::move(operands),
      .flow = std::move(flow),
      .execution = std::move(execution),
  });
  track_.addressIndex.add(address, commandIndex);
  return track_.commands.back();
}

}  // namespace vgmtrans::core
