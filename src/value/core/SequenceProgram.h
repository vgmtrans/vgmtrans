/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MetadataModel.h"

#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vgmtrans::core {

struct DialectId {
  std::string value;

  [[nodiscard]] bool valid() const noexcept { return !value.empty(); }
  friend bool operator==(const DialectId&, const DialectId&) noexcept = default;
};

struct CommandId {
  u32 value = invalidIdValue;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != invalidIdValue; }
  friend constexpr bool operator==(CommandId, CommandId) noexcept = default;
};

struct CommandHandlerId {
  u32 value = invalidIdValue;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != invalidIdValue; }
  friend constexpr bool operator==(CommandHandlerId, CommandHandlerId) noexcept = default;
};

struct CommandKindId {
  u32 value = invalidIdValue;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != invalidIdValue; }
  friend constexpr bool operator==(CommandKindId, CommandKindId) noexcept = default;
};

struct ByteSpan {
  u32 offset = 0;
  u32 size = 0;
};

struct OperandSpan {
  u32 offset = 0;
  u32 size = 0;
};

using CommandOperandValue = std::variant<u64, s64, std::string, Address>;

struct CommandOperand {
  std::string name;
  CommandOperandValue value;
  SourceRange range;
};

struct SourceCommand {
  CommandId id;
  CommandHandlerId handler;
  CommandKindId kind;
  u8 opcode = 0;
  SourceRange range;
  ByteSpan bytes;
  OperandSpan operands;
};

struct DecodeFlow {
  std::optional<Address> fallthrough;
  std::vector<Address> staticTargets;
  bool terminal = false;
};

class CommandReader {
public:
  CommandReader(SourceRange commandRange, std::span<const u8> bytes, std::vector<CommandOperand>* operands = nullptr);

  [[nodiscard]] u8 opcode() const;
  [[nodiscard]] size_t position() const noexcept { return position_; }
  [[nodiscard]] bool done() const noexcept { return position_ == bytes_.size(); }
  [[nodiscard]] std::span<const u8> remainingBytes() const noexcept;

  [[nodiscard]] u8 u8(std::string_view name);
  [[nodiscard]] s8 s8(std::string_view name);
  [[nodiscard]] u16 le16(std::string_view name);
  [[nodiscard]] s16 leS16(std::string_view name);
  [[nodiscard]] Address le16Address(std::string_view name);
  [[nodiscard]] u32 le24(std::string_view name);
  [[nodiscard]] u32 varLen(std::string_view name);
  [[nodiscard]] u16 be16(std::string_view name);
  [[nodiscard]] Address be16Address(std::string_view name);
  [[nodiscard]] std::string rawBytes(std::string_view name, size_t size);
  [[nodiscard]] std::string rawRemainingBytes(std::string_view name);
  void derived(std::string_view name, CommandOperandValue value);

private:
  [[nodiscard]] ::u8 readByte();
  void require(size_t size) const;
  void operand(std::string_view name, CommandOperandValue value, size_t begin, size_t size);
  [[nodiscard]] SourceRange operandRange(size_t begin, size_t size) const;

  SourceRange commandRange_;
  std::span<const ::u8> bytes_;
  std::vector<CommandOperand>* operands_ = nullptr;
  size_t position_ = 1;
};

struct AddressIndex {
  std::vector<std::pair<Address, u32>> entries;

  void add(Address address, u32 commandIndex);
  [[nodiscard]] std::optional<u32> find(Address address) const;
};

struct TrackProgram {
  TrackId id;
  u32 sourceTrackNumber = 0;
  Address startAddress;
  std::vector<SourceCommand> commands;
  AddressIndex addressIndex;

  // Command bytes and operands are pooled at track scope so the parsed snapshot
  // avoids one heap allocation per command.
  std::vector<u8> commandBytes;
  std::vector<CommandOperand> operands;

  [[nodiscard]] std::span<const u8> bytesFor(const SourceCommand& command) const;
  [[nodiscard]] std::span<const CommandOperand> operandsFor(const SourceCommand& command) const;
};

struct SequenceInstrumentRef {
  std::optional<AssetId> asset;
  u32 bank = 0;
  u32 program = 0;
  std::optional<SourceRange> range;
};

struct SequenceProgramBehavior {
  LoopPolicy defaultLoopPolicy = LoopPolicy::Default;
  // Zero means "use the next default": program -> dialect -> VM fallback.
  u32 commandLimit = 0;
  // Some legacy drivers rely on channel defaults that are not source opcodes.
  // Keep them in behavior so formats opt in explicitly and exporters can emit
  // stable initialization without attaching it to a fake source command.
  std::optional<double> initialReverbSend;
  std::optional<u8> initialMonoModeChannels;
  // Some tick-by-tick drivers stop every track as soon as any track exhausts
  // the export loop budget. Formats opt in when that global stop is part of
  // the source driver's playback model.
  bool stopAllTracksAtFirstLoop = false;
};

struct SequenceProgram {
  DialectId dialect;
  Timebase timebase;
  Address sourceBaseAddress;
  SequenceProgramBehavior behavior;
  std::vector<TrackProgram> tracks;
  std::vector<SequenceInstrumentRef> referencedInstruments;
};

[[nodiscard]] const TrackProgram* trackById(const SequenceProgram& program, TrackId id);
[[nodiscard]] const SourceCommand* sourceCommandById(const TrackProgram& track, CommandId id);
[[nodiscard]] std::optional<u64> commandOperandU64(std::span<const CommandOperand> operands, std::string_view name);
void addUniqueReferencedInstrument(SequenceProgram& program, std::optional<AssetId> asset, u32 bank, u32 programNumber,
                                   std::optional<SourceRange> range);
void addBankedProgramReference(SequenceProgram& program, const TrackProgram& track, const SourceCommand& command,
                               CommandKindId programKind, std::string_view operandName,
                               std::optional<AssetId> instrumentSetId);

struct SequenceProgramAsset {
  AssetMetadata metadata;
  SequenceProgram program;
};

class TrackProgramBuilder {
public:
  explicit TrackProgramBuilder(TrackProgram& track);

  template <class Command>
  const SourceCommand& add(CommandHandlerId handler, CommandKindId kind, Address address, SourceRange range,
                           std::span<const u8> bytes) {
    std::vector<CommandOperand> decodedOperands;
    CommandReader reader{range, bytes, &decodedOperands};
    static_cast<void>(Command::parse(reader));
    if (!reader.done()) {
      throw std::invalid_argument("Sequence command parser left trailing source bytes");
    }
    return addDecoded(handler, kind, address, range, bytes, decodedOperands);
  }

  const SourceCommand& addDecoded(CommandHandlerId handler, CommandKindId kind, Address address, SourceRange range,
                                  std::span<const u8> bytes, std::span<const CommandOperand> operands);

private:
  TrackProgram& track_;
};

}  // namespace vgmtrans::core
