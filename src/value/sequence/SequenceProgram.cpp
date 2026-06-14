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

namespace {

[[nodiscard]] std::string hexBytes(std::span<const ::u8> bytes) {
  static constexpr char kDigits[] = "0123456789ABCDEF";

  std::string out;
  out.reserve(bytes.size() * 3);
  for (const ::u8 byte : bytes) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out.push_back(kDigits[byte >> 4]);
    out.push_back(kDigits[byte & 0x0f]);
  }
  return out;
}

}  // namespace

CommandReader::CommandReader(SourceRange commandRange, std::span<const ::u8> bytes,
                             std::vector<CommandOperand>* operands)
    : commandRange_(commandRange), bytes_(bytes), operands_(operands) {
  if (bytes_.empty()) {
    throw std::out_of_range("Sequence command bytes must include an opcode");
  }
}

u8 CommandReader::opcode() const {
  return bytes_.front();
}

std::span<const u8> CommandReader::remainingBytes() const noexcept {
  return bytes_.subspan(position_);
}

u8 CommandReader::u8(std::string_view name) {
  const size_t begin = position_;
  const ::u8 value = readByte();
  operand(name, static_cast<u64>(value), begin, 1);
  return value;
}

s8 CommandReader::s8(std::string_view name) {
  const size_t begin = position_;
  const auto value = static_cast<::s8>(readByte());
  operand(name, static_cast<s64>(value), begin, 1);
  return value;
}

u16 CommandReader::le16(std::string_view name) {
  const size_t begin = position_;
  const u16 low = readByte();
  const u16 high = readByte();
  const auto value = static_cast<u16>(low | (high << 8));
  operand(name, static_cast<u64>(value), begin, 2);
  return value;
}

s16 CommandReader::leS16(std::string_view name) {
  const size_t begin = position_;
  const auto value = static_cast<s16>(le16(name));
  if (operands_ != nullptr && !operands_->empty()) {
    operands_->back().value = static_cast<s64>(value);
    operands_->back().range = operandRange(begin, 2);
  }
  return value;
}

Address CommandReader::le16Address(std::string_view name) {
  const size_t begin = position_;
  const Address value{.value = le16(name)};
  if (operands_ != nullptr && !operands_->empty()) {
    operands_->back().value = value;
    operands_->back().range = operandRange(begin, 2);
  }
  return value;
}

u32 CommandReader::le24(std::string_view name) {
  const size_t begin = position_;
  const u32 low = readByte();
  const u32 middle = readByte();
  const u32 high = readByte();
  const u32 value = low | (middle << 8) | (high << 16);
  operand(name, static_cast<u64>(value), begin, 3);
  return value;
}

u32 CommandReader::varLen(std::string_view name) {
  const size_t begin = position_;
  u32 value = 0;
  while (position_ < bytes_.size()) {
    const ::u8 byte = readByte();
    value = (value << 7) + (byte & 0x7f);
    if ((byte & 0x80) == 0) {
      break;
    }
  }
  operand(name, static_cast<u64>(value), begin, position_ - begin);
  return value;
}

u16 CommandReader::be16(std::string_view name) {
  const size_t begin = position_;
  const u16 high = readByte();
  const u16 low = readByte();
  const auto value = static_cast<u16>((high << 8) | low);
  operand(name, static_cast<u64>(value), begin, 2);
  return value;
}

Address CommandReader::be16Address(std::string_view name) {
  const size_t begin = position_;
  const Address value{.value = be16(name)};
  if (operands_ != nullptr && !operands_->empty()) {
    operands_->back().value = value;
    operands_->back().range = operandRange(begin, 2);
  }
  return value;
}

std::string CommandReader::rawBytes(std::string_view name, size_t size) {
  const size_t begin = position_;
  require(size);
  const auto bytes = bytes_.subspan(position_, size);
  position_ += size;

  auto value = hexBytes(bytes);
  operand(name, value, begin, size);
  return value;
}

std::string CommandReader::rawRemainingBytes(std::string_view name) {
  return rawBytes(name, remainingBytes().size());
}

void CommandReader::derived(std::string_view name, CommandOperandValue value) {
  operand(name, std::move(value), 0, 1);
}

::u8 CommandReader::readByte() {
  require(1);
  const ::u8 value = bytes_[position_];
  ++position_;
  return value;
}

void CommandReader::require(size_t size) const {
  if (position_ + size > bytes_.size()) {
    throw std::out_of_range("Sequence command ended before all operands were decoded");
  }
}

void CommandReader::operand(std::string_view name, CommandOperandValue value, size_t begin, size_t size) {
  if (operands_ == nullptr) {
    return;
  }

  operands_->push_back(CommandOperand{
      .name = std::string(name),
      .value = std::move(value),
      .range = operandRange(begin, size),
  });
}

SourceRange CommandReader::operandRange(size_t begin, size_t size) const {
  if (!commandRange_.valid()) {
    return commandRange_;
  }

  return SourceRange{
      .source = commandRange_.source,
      .offset = commandRange_.offset + begin,
      .size = size,
  };
}

void AddressIndex::add(Address address, u32 commandIndex) {
  entries.emplace_back(address, commandIndex);
}

std::optional<u32> AddressIndex::find(Address address) const {
  const auto found =
      std::ranges::find_if(entries, [address](const auto& entry) { return entry.first.value == address.value; });
  if (found == entries.end()) {
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

std::span<const CommandOperand> TrackProgram::operandsFor(const SourceCommand& command) const {
  if (command.operands.offset + command.operands.size > operands.size()) {
    throw std::out_of_range("SourceCommand operand span is outside its TrackProgram pool");
  }
  return std::span<const CommandOperand>(operands).subspan(command.operands.offset, command.operands.size);
}

const TrackProgram* trackById(const SequenceProgram& program, TrackId id) {
  const auto found = std::ranges::find_if(program.tracks, [id](const TrackProgram& track) { return track.id == id; });
  if (found == program.tracks.end()) {
    return nullptr;
  }
  return &*found;
}

const SourceCommand* sourceCommandById(const TrackProgram& track, CommandId id) {
  const auto found =
      std::ranges::find_if(track.commands, [id](const SourceCommand& command) { return command.id == id; });
  if (found == track.commands.end()) {
    return nullptr;
  }
  return &*found;
}

std::optional<u64> commandOperandU64(std::span<const CommandOperand> operands, std::string_view name) {
  const auto found =
      std::ranges::find_if(operands, [name](const CommandOperand& operand) { return operand.name == name; });
  if (found == operands.end()) {
    return std::nullopt;
  }
  if (const auto* value = std::get_if<u64>(&found->value)) {
    return *value;
  }
  return std::nullopt;
}

void addUniqueReferencedInstrument(SequenceProgram& program, std::optional<AssetId> asset, u32 bank, u32 programNumber,
                                   std::optional<SourceRange> range) {
  const auto duplicate =
      std::ranges::any_of(program.referencedInstruments, [asset, bank, programNumber](const auto& ref) {
        return ref.asset == asset && ref.bank == bank && ref.program == programNumber;
      });
  if (duplicate) {
    return;
  }

  program.referencedInstruments.push_back(SequenceInstrumentRef{
      .asset = asset,
      .bank = bank,
      .program = programNumber,
      .range = std::move(range),
  });
}

void addBankedProgramReference(SequenceProgram& program, const TrackProgram& track, const SourceCommand& command,
                               CommandKindId programKind, std::string_view operandName,
                               std::optional<AssetId> instrumentSetId) {
  if (command.kind != programKind) {
    return;
  }

  const auto rawProgram = commandOperandU64(track.operandsFor(command), operandName);
  if (!rawProgram) {
    return;
  }

  addUniqueReferencedInstrument(program, instrumentSetId, static_cast<u32>(*rawProgram >> 7),
                                static_cast<u32>(*rawProgram & 0x7f), command.range);
}

TrackProgramBuilder::TrackProgramBuilder(TrackProgram& track) : track_(track) {
}

const SourceCommand& TrackProgramBuilder::addDecoded(CommandHandlerId handler, CommandKindId kind, Address address,
                                                     SourceRange range, std::span<const u8> bytes,
                                                     std::span<const CommandOperand> operands) {
  if (bytes.empty()) {
    throw std::invalid_argument("Sequence source commands must include an opcode byte");
  }

  const auto commandIndex = static_cast<u32>(track_.commands.size());
  const auto byteOffset = static_cast<u32>(track_.commandBytes.size());
  track_.commandBytes.insert(track_.commandBytes.end(), bytes.begin(), bytes.end());

  const auto operandOffset = static_cast<u32>(track_.operands.size());
  track_.operands.insert(track_.operands.end(), operands.begin(), operands.end());

  track_.commands.push_back(SourceCommand{
      .id = CommandId{commandIndex},
      .handler = handler,
      .kind = kind,
      .opcode = bytes.front(),
      .address = address,
      .encodedSize = static_cast<u32>(bytes.size()),
      .range = range,
      .bytes = ByteSpan{.offset = byteOffset, .size = static_cast<u32>(bytes.size())},
      .operands = OperandSpan{.offset = operandOffset, .size = static_cast<u32>(operands.size())},
  });
  track_.addressIndex.add(address, commandIndex);
  return track_.commands.back();
}

}  // namespace vgmtrans::core
