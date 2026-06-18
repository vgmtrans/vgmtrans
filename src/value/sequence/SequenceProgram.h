/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"
#include "value/model/SourceMap.h"

#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vgmtrans::core {

struct DialectId {
  std::string value;

  [[nodiscard]] bool valid() const noexcept { return !value.empty(); }
  friend bool operator==(const DialectId&, const DialectId&) noexcept = default;
};

struct ByteSpan {
  u32 offset = 0;
  u32 size = 0;
};

struct OperandSpan {
  u32 offset = 0;
  u32 size = 0;
};

using CommandOperandValue = std::variant<u64, s64, double, std::string, Address>;

struct CommandOperand {
  std::string name;
  CommandOperandValue value;
  SourceRange range;
};

// Describes what a command means for playback. This is metadata for UI and
// validation; execute() still implements the actual behavior.
enum class CommandPlaybackStatus {
  SourceOnly,
  NoOp,
  AffectsPlayback,
  AffectsControlFlow,
  StopsPlayback,
  Unsupported,
};

struct CommandKind {
  CommandKindId id;
  std::string kindName;
  std::string name;
  std::string detailKind;
  SequenceSemantic semantic = SequenceSemantic::Unknown;
  CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::AffectsPlayback;
};

// One decoded source opcode. It keeps the original bytes, named operands, source
// range, and the handler ID used to find the driver-specific behavior.
struct SourceCommand {
  CommandId id;
  CommandHandlerId handler;
  CommandKindId kind;
  u8 opcode = 0;
  Address address;
  u32 encodedSize = 0;
  SourceRange range;
  ByteSpan bytes;
  OperandSpan operands;
};

// Where decoding can continue after an opcode. Walkers use this before playback
// so jumps and calls can point at commands that have already been decoded.
struct DecodeFlow {
  enum class Kind {
    Fallthrough,
    Jump,
    Call,
    Return,
    Terminal,
  };

  Kind kind = Kind::Fallthrough;
  std::optional<Address> fallthrough;
  std::vector<Address> staticTargets;
  bool terminal = false;

  [[nodiscard]] static DecodeFlow fallthroughTo(Address next) {
    return DecodeFlow{
        .kind = Kind::Fallthrough,
        .fallthrough = next,
    };
  }

  [[nodiscard]] static DecodeFlow jump(Address destination) {
    return DecodeFlow{
        .kind = Kind::Jump,
        .staticTargets = {destination},
    };
  }

  [[nodiscard]] static DecodeFlow call(Address destination, Address returnAddress) {
    return DecodeFlow{
        .kind = Kind::Call,
        .fallthrough = returnAddress,
        .staticTargets = {destination},
    };
  }

  [[nodiscard]] static DecodeFlow return_() { return DecodeFlow{.kind = Kind::Return}; }

  [[nodiscard]] static DecodeFlow terminalFlow() {
    return DecodeFlow{
        .kind = Kind::Terminal,
        .terminal = true,
    };
  }

  [[nodiscard]] bool unconditionalJump() const noexcept { return kind == Kind::Jump && !staticTargets.empty(); }
  [[nodiscard]] bool callTarget() const noexcept { return kind == Kind::Call && !staticTargets.empty(); }
};

// Command structs use this to read operands. If an operand vector is supplied,
// each read also records a name, value, and source byte range for the UI.
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

// Maps source addresses to command indexes. The VM uses this for jumps, calls,
// and finding the next command by source address when vector order is different.
struct AddressIndex {
  std::unordered_map<u64, u32> commandByAddress;

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
  std::vector<CommandKind> commandKinds;
  std::vector<u8> commandBytes;
  std::vector<CommandOperand> operands;

  [[nodiscard]] const CommandKind* kind(CommandKindId id) const;
  [[nodiscard]] const CommandKind* kindForName(std::string_view kindName) const;
  [[nodiscard]] std::span<const u8> bytesFor(const SourceCommand& command) const;
  [[nodiscard]] std::span<const CommandOperand> operandsFor(const SourceCommand& command) const;
};

struct SequenceInstrumentRef {
  std::optional<AssetId> asset;
  u32 bank = 0;
  u32 program = 0;
  std::optional<SourceRange> range;
};

// Driver settings that affect playback but are not individual source commands,
// such as loop policy or initial channel state.
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
void addUniqueReferencedInstrument(SequenceProgram& program, std::optional<AssetId> asset, u32 bank, u32 programNumber,
                                   std::optional<SourceRange> range);

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
    // Decode once while adding the command, using the same parser that will be
    // used later to describe or execute it.
    CommandReader reader{range, bytes, &decodedOperands};
    static_cast<void>(Command::parse(reader));
    if (!reader.done()) {
      throw std::invalid_argument("Sequence command parser left trailing source bytes");
    }
    return addDecoded(handler, kind, address, range, bytes, decodedOperands);
  }

  const SourceCommand& addDecoded(CommandHandlerId handler, CommandKindId kind, Address address, SourceRange range,
                                  std::span<const u8> bytes, std::span<const CommandOperand> operands);
  const SourceCommand& addDecoded(CommandHandlerId handler, const CommandKind& kind, Address address, SourceRange range,
                                  std::span<const u8> bytes, std::span<const CommandOperand> operands);

private:
  [[nodiscard]] CommandKindId addOrReuseKind(const CommandKind& kind);

  TrackProgram& track_;
};

}  // namespace vgmtrans::core
