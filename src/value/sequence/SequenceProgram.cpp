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
  if (!id.valid() || id.value >= track.commands.size()) {
    return nullptr;
  }
  return &track.commands[id.value];
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

CommandId TrackProgram::addCommand(Address address, u8 opcode, SourceRange range,
                                   std::vector<SemanticOperand> operands, CommandFlow flow,
                                   SourceAnnotationId annotation, CommandExecution execution,
                                   SequenceSemantic semantic) {
  if (addressIndex.find(address)) {
    throw std::invalid_argument("Sequence command address was decoded more than once");
  }

  const auto commandIndex = static_cast<u32>(commands.size());
  commands.push_back(SourceCommand{
      .opcode = opcode,
      .address = address,
      .range = range,
      .annotation = annotation,
      .semantic = semantic,
      .operands = std::move(operands),
      .flow = std::move(flow),
      .execution = std::move(execution),
  });
  addressIndex.add(address, commandIndex);
  return CommandId{commandIndex};
}

}  // namespace vgmtrans::core
