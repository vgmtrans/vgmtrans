/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/SequenceDialect.h"
#include "value/core/SequenceCommandHelpers.h"
#include "value/core/Source.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vgmtrans::core {

// Temporary decoded form used while a bytecode decoder is deciding control flow.
// TrackProgramBuilder still owns the final immutable source-command snapshot.
struct DecodedBytecodeCommand {
  CommandHandlerId handler;
  CommandKindId kind;
  SourceRange range;
  std::vector<u8> bytes;
  std::vector<CommandOperand> operands;
  DecodeFlow flow;
};

template <class Command>
struct ParsedBytecodeCommand {
  DecodedBytecodeCommand decoded;
  Command command;
};

struct BytecodeDecodeContext {
  u32 bytecodeEnd = std::numeric_limits<u32>::max();
  u32 sequenceOffset = 0;
  u32 sequenceEnd = std::numeric_limits<u32>::max();
  u32 commandEnd = 0;
};

struct BytecodeCommandSpec;

using DecodeBytecodeCommand = DecodedBytecodeCommand (*)(const BytecodeCommandSpec& spec,
                                                         const BytecodeCommandSpec& truncatedSpec, ByteReader reader,
                                                         u32 begin, BytecodeDecodeContext context);

struct BytecodeCommandSpec {
  CommandHandlerId handler;
  CommandKindId kind;
  std::string kindName;
  std::string name;
  u32 fixedOperandBytes = 0;
  DecodeBytecodeCommand decode = nullptr;
};

struct BytecodeCommandOptions {
  std::optional<std::string_view> suffix;
};

struct FixedOperandByteCount {
  u32 value = 0;
};

struct PreservedBytecodeCommand {
  u8 opcode = 0;
  std::string_view displayName;
  FixedOperandByteCount operandBytes;
  BytecodeCommandOptions options;
};

[[nodiscard]] constexpr BytecodeCommandOptions suffix(std::string_view value) {
  return BytecodeCommandOptions{.suffix = value};
}

[[nodiscard]] constexpr FixedOperandByteCount operandBytes(u32 value) {
  return FixedOperandByteCount{.value = value};
}

[[nodiscard]] constexpr PreservedBytecodeCommand preservedOpcode(u8 opcode, std::string_view displayName,
                                                                 FixedOperandByteCount operandBytes = {},
                                                                 BytecodeCommandOptions options = {}) {
  return PreservedBytecodeCommand{
      .opcode = opcode,
      .displayName = displayName,
      .operandBytes = operandBytes,
      .options = options,
  };
}

[[nodiscard]] inline std::string slugifyCommandName(std::string_view displayName) {
  std::string result;
  bool pendingHyphen = false;
  for (const char ch : displayName) {
    char out = '\0';
    if (ch >= 'A' && ch <= 'Z') {
      out = static_cast<char>(ch - 'A' + 'a');
    } else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
      out = ch;
    }

    if (out != '\0') {
      if (pendingHyphen && !result.empty()) {
        result.push_back('-');
      }
      result.push_back(out);
      pendingHyphen = false;
    } else {
      pendingHyphen = !result.empty();
    }
  }
  return result;
}

[[nodiscard]] inline bool hasBytecodeBytes(ByteReader reader, u32 offset, u32 size, u32 end) {
  return offset <= end && size <= end - offset && reader.has(offset, size);
}

[[nodiscard]] inline DecodedBytecodeCommand recordSizedPreservedBytecodeCommand(const BytecodeCommandSpec& spec,
                                                                                ByteReader reader, u32 begin, u32 end) {
  const SourceRange range = reader.range(begin, end - begin);
  const auto bytes = reader.slice(begin, end - begin);
  std::vector<u8> ownedBytes{bytes.begin(), bytes.end()};
  std::vector<CommandOperand> operands;
  CommandReader commandReader{range, ownedBytes, &operands};
  const auto operandBytes = static_cast<u32>(end - begin - 1);
  if (operandBytes > 0) {
    static_cast<void>(commandReader.rawBytes("bytes", operandBytes));
  }
  if (!commandReader.done()) {
    throw std::invalid_argument("Preserved bytecode command left trailing source bytes");
  }
  return DecodedBytecodeCommand{
      .handler = spec.handler,
      .kind = spec.kind,
      .range = range,
      .bytes = std::move(ownedBytes),
      .operands = std::move(operands),
  };
}

template <class Command>
[[nodiscard]] DecodedBytecodeCommand recordMappedSizedBytecodeCommand(const BytecodeCommandSpec& spec,
                                                                      ByteReader reader, u32 begin, u32 end) {
  const SourceRange range = reader.range(begin, end - begin);
  const auto bytes = reader.slice(begin, end - begin);
  std::vector<u8> ownedBytes{bytes.begin(), bytes.end()};
  std::vector<CommandOperand> operands;
  CommandReader commandReader{range, ownedBytes, &operands};
  static_cast<void>(Command::parse(commandReader));
  if (!commandReader.done()) {
    throw std::invalid_argument("Sequence command parser left trailing source bytes");
  }
  return DecodedBytecodeCommand{
      .handler = spec.handler,
      .kind = spec.kind,
      .range = range,
      .bytes = std::move(ownedBytes),
      .operands = std::move(operands),
  };
}

// Parse from the available bytecode span, then shrink the saved source bytes to
// exactly the amount consumed by Command::parse().
template <class Command>
[[nodiscard]] std::optional<ParsedBytecodeCommand<Command>> parseMappedBytecodeCommand(const BytecodeCommandSpec& spec,
                                                                                       ByteReader reader, u32 begin,
                                                                                       u32 end) {
  if (!hasBytecodeBytes(reader, begin, 1, end)) {
    return std::nullopt;
  }

  const auto boundedEnd = static_cast<u32>(std::min<size_t>(reader.size(), end));
  if (begin >= boundedEnd) {
    return std::nullopt;
  }

  const u32 availableSize = boundedEnd - begin;
  const SourceRange availableRange = reader.range(begin, availableSize);
  const auto availableBytes = reader.slice(begin, availableSize);
  std::vector<CommandOperand> operands;
  CommandReader commandReader{availableRange, availableBytes, &operands};

  Command command;
  try {
    command = Command::parse(commandReader);
  } catch (const std::out_of_range&) {
    return std::nullopt;
  }

  const auto commandSize = static_cast<u32>(commandReader.position());
  const auto commandBytes = availableBytes.subspan(0, commandSize);
  std::vector<u8> ownedBytes{commandBytes.begin(), commandBytes.end()};
  return ParsedBytecodeCommand<Command>{
      .decoded =
          DecodedBytecodeCommand{
              .handler = spec.handler,
              .kind = spec.kind,
              .range = reader.range(begin, commandSize),
              .bytes = std::move(ownedBytes),
              .operands = std::move(operands),
          },
      .command = command,
  };
}

template <class Command>
[[nodiscard]] DecodedBytecodeCommand terminalMappedBytecodeCommand(const BytecodeCommandSpec& spec, ByteReader reader,
                                                                   u32 begin, u32 end) {
  auto decoded = recordMappedSizedBytecodeCommand<Command>(spec, reader, begin, end);
  decoded.flow = DecodeFlow::terminalFlow();
  return decoded;
}

template <class Command>
[[nodiscard]] DecodedBytecodeCommand truncatedMappedBytecodeCommand(const BytecodeCommandSpec& spec, ByteReader reader,
                                                                    u32 begin, u32 end) {
  const auto boundedEnd = static_cast<u32>(std::min<size_t>(reader.size(), end));
  return terminalMappedBytecodeCommand<Command>(spec, reader, begin, std::min(begin + 1, boundedEnd));
}

namespace detail {

template <class Command>
concept HasDecodeFlowWithContext = requires(const Command& command, const BytecodeDecodeContext& context) {
  { command.decodeFlow(context) } -> std::same_as<DecodeFlow>;
};

template <class Command>
concept HasDecodeFlow = requires(const Command& command) {
  { command.decodeFlow() } -> std::same_as<DecodeFlow>;
};

template <class Command>
[[nodiscard]] DecodeFlow commandDecodeFlow(const Command& command, const BytecodeDecodeContext& context) {
  if constexpr (HasDecodeFlowWithContext<Command>) {
    return command.decodeFlow(context);
  } else if constexpr (HasDecodeFlow<Command>) {
    return command.decodeFlow();
  } else {
    return DecodeFlow::fallthroughTo(Address{context.commandEnd});
  }
}

template <class Command>
[[nodiscard]] DecodedBytecodeCommand decodeMappedCommand(const BytecodeCommandSpec& spec,
                                                         const BytecodeCommandSpec& truncatedSpec, ByteReader reader,
                                                         u32 begin, BytecodeDecodeContext context) {
  auto parsed = parseMappedBytecodeCommand<Command>(spec, reader, begin, context.bytecodeEnd);
  if (!parsed) {
    return truncatedSpec.decode(truncatedSpec, truncatedSpec, reader, begin, context);
  }

  auto decoded = std::move(parsed->decoded);
  context.commandEnd = static_cast<u32>(decoded.range.endOffset());
  decoded.flow = commandDecodeFlow(parsed->command, context);
  return decoded;
}

template <class Command, Address Command::* Target>
[[nodiscard]] DecodedBytecodeCommand decodeMappedJumpCommand(const BytecodeCommandSpec& spec,
                                                             const BytecodeCommandSpec& truncatedSpec,
                                                             ByteReader reader, u32 begin,
                                                             BytecodeDecodeContext context) {
  static_assert(!HasDecodeFlowWithContext<Command> && !HasDecodeFlow<Command>,
                "Use either a map-level jump helper or Command::decodeFlow(), not both");
  auto parsed = parseMappedBytecodeCommand<Command>(spec, reader, begin, context.bytecodeEnd);
  if (!parsed) {
    return truncatedSpec.decode(truncatedSpec, truncatedSpec, reader, begin, context);
  }

  auto decoded = std::move(parsed->decoded);
  decoded.flow = DecodeFlow::jump(parsed->command.*Target);
  return decoded;
}

template <class Command, Address Command::* Target>
[[nodiscard]] DecodedBytecodeCommand decodeMappedCallCommand(const BytecodeCommandSpec& spec,
                                                             const BytecodeCommandSpec& truncatedSpec,
                                                             ByteReader reader, u32 begin,
                                                             BytecodeDecodeContext context) {
  static_assert(!HasDecodeFlowWithContext<Command> && !HasDecodeFlow<Command>,
                "Use either a map-level call helper or Command::decodeFlow(), not both");
  auto parsed = parseMappedBytecodeCommand<Command>(spec, reader, begin, context.bytecodeEnd);
  if (!parsed) {
    return truncatedSpec.decode(truncatedSpec, truncatedSpec, reader, begin, context);
  }

  auto decoded = std::move(parsed->decoded);
  decoded.flow = DecodeFlow::call(parsed->command.*Target, Address{static_cast<u32>(decoded.range.endOffset())});
  return decoded;
}

template <class Command>
[[nodiscard]] DecodedBytecodeCommand decodeMappedReturnCommand(const BytecodeCommandSpec& spec,
                                                               const BytecodeCommandSpec& truncatedSpec,
                                                               ByteReader reader, u32 begin,
                                                               BytecodeDecodeContext context) {
  static_assert(!HasDecodeFlowWithContext<Command> && !HasDecodeFlow<Command>,
                "Use either a map-level return helper or Command::decodeFlow(), not both");
  auto parsed = parseMappedBytecodeCommand<Command>(spec, reader, begin, context.bytecodeEnd);
  if (!parsed) {
    return truncatedSpec.decode(truncatedSpec, truncatedSpec, reader, begin, context);
  }

  auto decoded = std::move(parsed->decoded);
  decoded.flow = DecodeFlow::return_();
  return decoded;
}

template <class Command>
[[nodiscard]] DecodedBytecodeCommand decodeMappedTerminalCommand(const BytecodeCommandSpec& spec,
                                                                 const BytecodeCommandSpec& truncatedSpec,
                                                                 ByteReader reader, u32 begin,
                                                                 BytecodeDecodeContext context) {
  static_assert(!HasDecodeFlowWithContext<Command> && !HasDecodeFlow<Command>,
                "Use either a map-level terminal helper or Command::decodeFlow(), not both");
  auto parsed = parseMappedBytecodeCommand<Command>(spec, reader, begin, context.bytecodeEnd);
  if (!parsed) {
    return truncatedSpec.decode(truncatedSpec, truncatedSpec, reader, begin, context);
  }

  auto decoded = std::move(parsed->decoded);
  decoded.flow = DecodeFlow::terminalFlow();
  return decoded;
}

template <class Command>
[[nodiscard]] DecodedBytecodeCommand decodeMappedTruncatedCommand(const BytecodeCommandSpec& spec,
                                                                  const BytecodeCommandSpec&, ByteReader reader,
                                                                  u32 begin, BytecodeDecodeContext context) {
  return truncatedMappedBytecodeCommand<Command>(spec, reader, begin, context.bytecodeEnd);
}

inline DecodedBytecodeCommand decodeMappedPreservedCommand(const BytecodeCommandSpec& spec,
                                                           const BytecodeCommandSpec& truncatedSpec, ByteReader reader,
                                                           u32 begin, BytecodeDecodeContext context) {
  const u32 operandOffset = begin + 1;
  if (!hasBytecodeBytes(reader, operandOffset, spec.fixedOperandBytes, context.bytecodeEnd)) {
    return truncatedSpec.decode(truncatedSpec, truncatedSpec, reader, begin, context);
  }

  const u32 end = operandOffset + spec.fixedOperandBytes;
  auto decoded = recordSizedPreservedBytecodeCommand(spec, reader, begin, end);
  decoded.flow = DecodeFlow::fallthroughTo(Address{end});
  return decoded;
}

}  // namespace detail

inline void appendDecodedBytecodeCommand(TrackProgramBuilder& builder, const DecodedBytecodeCommand& decoded,
                                         u32 offset) {
  builder.addDecoded(decoded.handler, decoded.kind, Address{offset}, decoded.range, decoded.bytes, decoded.operands);
}

struct BytecodeRangeSpec {
  u8 first = 0;
  u8 last = 0;
  BytecodeCommandSpec spec;
};

struct BytecodeDispatchTable {
  std::array<std::optional<BytecodeCommandSpec>, 256> opcodes;
  std::vector<BytecodeRangeSpec> ranges;
  std::optional<BytecodeCommandSpec> unknown;
  std::optional<BytecodeCommandSpec> truncated;

  [[nodiscard]] DecodedBytecodeCommand decode(ByteReader reader, u32 begin, BytecodeDecodeContext context = {}) const {
    if (context.bytecodeEnd == std::numeric_limits<u32>::max()) {
      context.bytecodeEnd = static_cast<u32>(reader.size());
    }
    if (context.sequenceEnd == std::numeric_limits<u32>::max()) {
      context.sequenceEnd = context.bytecodeEnd;
    }
    if (!truncated) {
      throw std::logic_error("Bytecode dispatch table has no truncated-command fallback");
    }

    const u8 opcode = reader.u8At(begin);
    if (const auto& spec = opcodes[opcode]) {
      return spec->decode(*spec, *truncated, reader, begin, context);
    }
    for (const BytecodeRangeSpec& range : ranges) {
      if (opcode >= range.first && opcode <= range.last) {
        return range.spec.decode(range.spec, *truncated, reader, begin, context);
      }
    }
    if (unknown) {
      return unknown->decode(*unknown, *truncated, reader, begin, context);
    }
    return truncated->decode(*truncated, *truncated, reader, begin, context);
  }
};

template <class TrackState, class Context>
class BytecodeMapBuilder {
public:
  BytecodeMapBuilder(std::string prefix, SequenceDialectBuilder<TrackState, Context>& dialectBuilder)
      : prefix_(std::move(prefix)), dialectBuilder_(&dialectBuilder) {}

  BytecodeMapBuilder(std::string prefix, const SequenceDialect& dialect)
      : prefix_(std::move(prefix)), dialect_(&dialect) {}

  template <u8 Op, class Command>
  BytecodeMapBuilder& op(std::string_view displayName, BytecodeCommandOptions options = {}) {
    return op<Command>(Op, displayName, options);
  }

  template <class Command>
  BytecodeMapBuilder& op(u8 opcode, std::string_view displayName, BytecodeCommandOptions options = {}) {
    return addOpcode(opcode, commandSpec<Command>(displayName, options, detail::decodeMappedCommand<Command>));
  }

  template <u8 First, u8 Last, class Command>
  BytecodeMapBuilder& range(std::string_view displayName, BytecodeCommandOptions options = {}) {
    static_assert(First <= Last);
    const auto spec = commandSpec<Command>(displayName, options, detail::decodeMappedCommand<Command>);
    ranges_.push_back(BytecodeRangeSpec{.first = First, .last = Last, .spec = spec});
    return *this;
  }

  template <u8 Op, class Command, Address Command::* Target>
  BytecodeMapBuilder& jump(std::string_view displayName, BytecodeCommandOptions options = {}) {
    return addOpcode(Op, commandSpec<Command>(displayName, options, detail::decodeMappedJumpCommand<Command, Target>));
  }

  template <u8 Op, class Command, Address Command::* Target>
  BytecodeMapBuilder& call(std::string_view displayName, BytecodeCommandOptions options = {}) {
    return addOpcode(Op, commandSpec<Command>(displayName, options, detail::decodeMappedCallCommand<Command, Target>));
  }

  template <u8 Op, class Command>
  BytecodeMapBuilder& returns(std::string_view displayName, BytecodeCommandOptions options = {}) {
    return addOpcode(Op, commandSpec<Command>(displayName, options, detail::decodeMappedReturnCommand<Command>));
  }

  template <u8 Op, class Command>
  BytecodeMapBuilder& terminal(std::string_view displayName, BytecodeCommandOptions options = {}) {
    return addOpcode(Op, commandSpec<Command>(displayName, options, detail::decodeMappedTerminalCommand<Command>));
  }

  BytecodeMapBuilder& preserved(u8 opcode, std::string_view displayName, FixedOperandByteCount operandBytes = {},
                                BytecodeCommandOptions options = {}) {
    return addOpcode(opcode, preservedSpec(displayName, operandBytes, options));
  }

  BytecodeMapBuilder& preserved(u8 opcode, std::string_view displayName, BytecodeCommandOptions options) {
    return preserved(opcode, displayName, FixedOperandByteCount{}, options);
  }

  BytecodeMapBuilder& preserved(std::span<const PreservedBytecodeCommand> commands) {
    for (const PreservedBytecodeCommand& command : commands) {
      preserved(command.opcode, command.displayName, command.operandBytes, command.options);
    }
    return *this;
  }

  template <class Command>
  BytecodeMapBuilder& unknown(std::string_view displayName, BytecodeCommandOptions options = {}) {
    table_.unknown = commandSpec<Command>(displayName, options, detail::decodeMappedTruncatedCommand<Command>);
    if (!table_.truncated) {
      table_.truncated = table_.unknown;
    }
    return *this;
  }

  template <class Command>
  BytecodeMapBuilder& truncated(std::string_view displayName, BytecodeCommandOptions options = {}) {
    table_.truncated = commandSpec<Command>(displayName, options, detail::decodeMappedTruncatedCommand<Command>);
    return *this;
  }

  [[nodiscard]] BytecodeCommandSpec preservedSpec(std::string_view displayName, FixedOperandByteCount operandBytes = {},
                                                  BytecodeCommandOptions options = {}) {
    const std::string kindName = makeKind(displayName, options);
    const auto [handler, kind] = preservedHandler(kindName, displayName);
    return BytecodeCommandSpec{
        .handler = handler,
        .kind = kind,
        .kindName = kindName,
        .name = std::string(displayName),
        .fixedOperandBytes = operandBytes.value,
        .decode = detail::decodeMappedPreservedCommand,
    };
  }

  [[nodiscard]] BytecodeDispatchTable finish() {
    validateNoOverlaps();
    table_.ranges = std::move(ranges_);
    if (!table_.truncated && table_.unknown) {
      table_.truncated = table_.unknown;
    }
    if (!table_.truncated) {
      throw std::logic_error("Bytecode dispatch table requires truncated() or unknown()");
    }
    return std::move(table_);
  }

private:
  using HandlerTypeToken = const void*;

  struct HandlerCacheEntry {
    CommandHandlerId handler;
    CommandKindId kind;
    HandlerTypeToken commandType = nullptr;
    std::string name;
    bool preserved = false;
  };

  template <class Command>
  [[nodiscard]] static HandlerTypeToken commandTypeToken() {
    static const int token = 0;
    return &token;
  }

  BytecodeMapBuilder& addOpcode(u8 opcode, BytecodeCommandSpec spec) {
    if (table_.opcodes[opcode]) {
      throw std::logic_error("Bytecode opcode was registered twice");
    }
    table_.opcodes[opcode] = std::move(spec);
    return *this;
  }

  void validateNoOverlaps() const {
    std::array<bool, 256> occupied{};
    for (u32 opcode = 0; opcode < table_.opcodes.size(); ++opcode) {
      occupied[opcode] = table_.opcodes[opcode].has_value();
    }

    // Exact opcode entries and ranges are both source-driver declarations. Make
    // ambiguity explicit instead of relying on dispatch precedence.
    for (const BytecodeRangeSpec& range : ranges_) {
      for (u32 opcode = range.first; opcode <= range.last; ++opcode) {
        if (occupied[opcode]) {
          throw std::logic_error("Bytecode opcode range overlaps another mapping");
        }
        occupied[opcode] = true;
      }
    }
  }

  [[nodiscard]] std::string makeKind(std::string_view displayName, const BytecodeCommandOptions& options) const {
    const std::string suffix = options.suffix ? std::string{*options.suffix} : slugifyCommandName(displayName);
    return prefix_ + "." + suffix;
  }

  template <class Command>
  [[nodiscard]] BytecodeCommandSpec commandSpec(std::string_view displayName, BytecodeCommandOptions options,
                                                DecodeBytecodeCommand decode) {
    const std::string kindName = makeKind(displayName, options);
    const auto [handler, kind] = commandHandler<Command>(kindName, displayName);
    return BytecodeCommandSpec{
        .handler = handler,
        .kind = kind,
        .kindName = kindName,
        .name = std::string(displayName),
        .decode = decode,
    };
  }

  template <class Command>
  [[nodiscard]] std::pair<CommandHandlerId, CommandKindId> commandHandler(const std::string& kindName,
                                                                          std::string_view displayName) {
    const HandlerTypeToken commandType = commandTypeToken<Command>();
    if (const auto found = handlers_.find(kindName); found != handlers_.end()) {
      const HandlerCacheEntry& entry = found->second;
      if (entry.preserved || entry.commandType != commandType || entry.name != displayName) {
        throw std::logic_error("Bytecode command kind reused with incompatible handler");
      }
      return {entry.handler, entry.kind};
    }

    CommandHandlerId handler;
    CommandKindId kind;
    if (dialectBuilder_ != nullptr) {
      handler = dialectBuilder_->template addCommand<Command>(kindName, displayName);
      kind = CommandKindId{handler.value};
    } else {
      const CommandHandler* found = dialect_->handlerForKind(kindName);
      if (found == nullptr) {
        throw std::logic_error("Bytecode command was not registered in its dialect");
      }
      handler = found->id;
      kind = found->kind;
    }
    handlers_[kindName] = HandlerCacheEntry{
        .handler = handler,
        .kind = kind,
        .commandType = commandType,
        .name = std::string(displayName),
        .preserved = false,
    };
    return {handler, kind};
  }

  [[nodiscard]] std::pair<CommandHandlerId, CommandKindId> preservedHandler(const std::string& kindName,
                                                                            std::string_view displayName) {
    if (const auto found = handlers_.find(kindName); found != handlers_.end()) {
      const HandlerCacheEntry& entry = found->second;
      if (!entry.preserved || entry.name != displayName) {
        throw std::logic_error("Preserved bytecode command kind reused with incompatible handler");
      }
      return {entry.handler, entry.kind};
    }

    CommandHandlerId handler;
    CommandKindId kind;
    if (dialectBuilder_ != nullptr) {
      handler = dialectBuilder_->addPreservedCommand(kindName, displayName);
      kind = CommandKindId{handler.value};
    } else {
      const CommandHandler* found = dialect_->handlerForKind(kindName);
      if (found == nullptr) {
        throw std::logic_error("Preserved bytecode command was not registered in its dialect");
      }
      handler = found->id;
      kind = found->kind;
    }
    handlers_[kindName] = HandlerCacheEntry{
        .handler = handler,
        .kind = kind,
        .name = std::string(displayName),
        .preserved = true,
    };
    return {handler, kind};
  }

  std::string prefix_;
  SequenceDialectBuilder<TrackState, Context>* dialectBuilder_ = nullptr;
  const SequenceDialect* dialect_ = nullptr;
  BytecodeDispatchTable table_;
  std::vector<BytecodeRangeSpec> ranges_;
  std::unordered_map<std::string, HandlerCacheEntry> handlers_;
};

}  // namespace vgmtrans::core
