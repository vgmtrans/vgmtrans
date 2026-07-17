/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceProgram.h"

#include <algorithm>
#include <stdexcept>
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

std::span<const u8> TrackProgram::bytesFor(const SourceCommand& command) const {
  if (command.bytes.offset + command.bytes.size > commandBytes.size()) {
    throw std::out_of_range("SourceCommand byte span is outside its TrackProgram pool");
  }
  return std::span<const u8>(commandBytes).subspan(command.bytes.offset, command.bytes.size);
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

const SemanticOperand* semanticOperand(const SourceCommand& command, SemanticOperandId id) {
  const auto found =
      std::ranges::find_if(command.operands, [id](const SemanticOperand& operand) { return operand.id == id; });
  return found != command.operands.end() ? &*found : nullptr;
}

TrackProgramBuilder::TrackProgramBuilder(TrackProgram& track) : track_(track) {
}

const SourceCommand& TrackProgramBuilder::addDecoded(Address address, SourceRange range, std::span<const u8> bytes,
                                                     SourceAnnotationId annotation, DecodeFlow flow) {
  if (bytes.empty()) {
    throw std::invalid_argument("Sequence source commands must include an opcode byte");
  }
  if (track_.addressIndex.find(address)) {
    throw std::invalid_argument("Sequence command address was decoded more than once");
  }

  const auto commandIndex = static_cast<u32>(track_.commands.size());
  const auto byteOffset = static_cast<u32>(track_.commandBytes.size());
  track_.commandBytes.insert(track_.commandBytes.end(), bytes.begin(), bytes.end());

  track_.commands.push_back(SourceCommand{
      .id = CommandId{commandIndex},
      .opcode = bytes.front(),
      .address = address,
      .encodedSize = static_cast<u32>(bytes.size()),
      .range = range,
      .annotation = annotation,
      .bytes = ByteSpan{.offset = byteOffset, .size = static_cast<u32>(bytes.size())},
      .flow = std::move(flow),
  });
  track_.addressIndex.add(address, commandIndex);
  return track_.commands.back();
}

const SourceCommand& TrackProgramBuilder::addSemantic(Address address, u8 opcode, u32 encodedSize, SourceRange range,
                                                      SemanticCommandKind kind, std::vector<SemanticOperand> operands,
                                                      DecodeFlow flow, SourceAnnotationId annotation) {
  if (encodedSize == 0) {
    throw std::invalid_argument("Semantic sequence commands must include an opcode byte");
  }
  if (!kind.valid()) {
    throw std::invalid_argument("Semantic sequence commands must have a command kind");
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
      .kind = kind,
      .operands = std::move(operands),
      .flow = std::move(flow),
  });
  track_.addressIndex.add(address, commandIndex);
  return track_.commands.back();
}

}  // namespace vgmtrans::core
