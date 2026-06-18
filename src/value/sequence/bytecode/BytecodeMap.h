/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/SequenceDialect.h"
#include "value/sequence/bytecode/BytecodeDecode.h"

#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vgmtrans::core {

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

// Builds the opcode table for one source driver. Format code calls op(), range(),
// jump(), call(), terminal(), and related helpers from one local map. When the
// dialect is built, the map assigns handler IDs; when a sequence is decoded, it
// looks those IDs up again so both paths stay tied to the same opcode list.
template <class TrackState, class Context>
class BytecodeMapBuilder {
public:
  BytecodeMapBuilder(std::string prefix, SequenceDialectBuilder<TrackState, Context>& dialectBuilder)
      : prefix_(std::move(prefix)), dialectBuilder_(&dialectBuilder) {}

  BytecodeMapBuilder(std::string prefix, const SequenceDialect& dialect)
      : prefix_(std::move(prefix)), dialect_(&dialect) {}

  template <u8 Op, class Command>
  BytecodeMapBuilder& op(BytecodeCommandOptions options = {}) {
    return op<Op, Command>(Command::meta, options);
  }

  template <u8 Op, class Command>
  BytecodeMapBuilder& op(std::string_view displayName, BytecodeCommandOptions options = {}) {
    return op<Command>(Op, displayName, options);
  }

  template <u8 Op, class Command>
  BytecodeMapBuilder& op(CommandMeta meta, BytecodeCommandOptions options = {}) {
    return op<Command>(Op, meta.displayName, optionsWithMeta(meta, options));
  }

  template <class Command>
  BytecodeMapBuilder& op(u8 opcode, BytecodeCommandOptions options = {}) {
    return op<Command>(opcode, Command::meta, options);
  }

  template <class Command>
  BytecodeMapBuilder& op(u8 opcode, std::string_view displayName, BytecodeCommandOptions options = {}) {
    return addOpcode(opcode, commandSpec<Command>(displayName, options, detail::decodeMappedCommand<Command>));
  }

  template <class Command>
  BytecodeMapBuilder& op(u8 opcode, CommandMeta meta, BytecodeCommandOptions options = {}) {
    return op<Command>(opcode, meta.displayName, optionsWithMeta(meta, options));
  }

  template <u8 First, u8 Last, class Command>
  BytecodeMapBuilder& range(BytecodeCommandOptions options = {}) {
    return range<First, Last, Command>(Command::meta.displayName, optionsWithMeta(Command::meta, options));
  }

  template <u8 First, u8 Last, class Command>
  BytecodeMapBuilder& range(std::string_view displayName, BytecodeCommandOptions options = {}) {
    static_assert(First <= Last);
    const auto spec = commandSpec<Command>(displayName, options, detail::decodeMappedCommand<Command>);
    ranges_.push_back(BytecodeRangeSpec{.first = First, .last = Last, .spec = spec});
    return *this;
  }

  template <u8 Op, class Command, Address Command::* Target>
  BytecodeMapBuilder& jump(BytecodeCommandOptions options = {}) {
    return jump<Op, Command, Target>(Command::meta.displayName, optionsWithMeta(Command::meta, options));
  }

  template <u8 Op, class Command, Address Command::* Target>
  BytecodeMapBuilder& jump(std::string_view displayName, BytecodeCommandOptions options = {}) {
    return addOpcode(Op, commandSpec<Command>(displayName, options, detail::decodeMappedJumpCommand<Command, Target>));
  }

  template <u8 Op, class Command, Address Command::* Target>
  BytecodeMapBuilder& call(BytecodeCommandOptions options = {}) {
    return call<Op, Command, Target>(Command::meta.displayName, optionsWithMeta(Command::meta, options));
  }

  template <u8 Op, class Command, Address Command::* Target>
  BytecodeMapBuilder& call(std::string_view displayName, BytecodeCommandOptions options = {}) {
    return addOpcode(Op, commandSpec<Command>(displayName, options, detail::decodeMappedCallCommand<Command, Target>));
  }

  template <u8 Op, class Command>
  BytecodeMapBuilder& returns(BytecodeCommandOptions options = {}) {
    return returns<Op, Command>(Command::meta.displayName, optionsWithMeta(Command::meta, options));
  }

  template <u8 Op, class Command>
  BytecodeMapBuilder& returns(std::string_view displayName, BytecodeCommandOptions options = {}) {
    return addOpcode(Op, commandSpec<Command>(displayName, options, detail::decodeMappedReturnCommand<Command>));
  }

  template <u8 Op, class Command>
  BytecodeMapBuilder& terminal(BytecodeCommandOptions options = {}) {
    return terminal<Op, Command>(Command::meta.displayName, optionsWithMeta(Command::meta, options));
  }

  template <u8 Op, class Command>
  BytecodeMapBuilder& terminal(std::string_view displayName, BytecodeCommandOptions options = {}) {
    return addOpcode(Op, commandSpec<Command>(displayName, options, detail::decodeMappedTerminalCommand<Command>));
  }

  template <u8 Op, class Command>
  BytecodeMapBuilder& terminal(CommandMeta meta, BytecodeCommandOptions options = {}) {
    return terminal<Op, Command>(meta.displayName, optionsWithMeta(meta, options));
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
  BytecodeMapBuilder& unknown(BytecodeCommandOptions options = {}) {
    return unknown<Command>(Command::meta.displayName, optionsWithMeta(Command::meta, options));
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
  BytecodeMapBuilder& unknown(CommandMeta meta, BytecodeCommandOptions options = {}) {
    return unknown<Command>(meta.displayName, optionsWithMeta(meta, options));
  }

  template <class Command>
  BytecodeMapBuilder& truncated(BytecodeCommandOptions options = {}) {
    return truncated<Command>(Command::meta.displayName, optionsWithMeta(Command::meta, options));
  }

  template <class Command>
  BytecodeMapBuilder& truncated(std::string_view displayName, BytecodeCommandOptions options = {}) {
    table_.truncated = commandSpec<Command>(displayName, options, detail::decodeMappedTruncatedCommand<Command>);
    return *this;
  }

  template <class Command>
  BytecodeMapBuilder& truncated(CommandMeta meta, BytecodeCommandOptions options = {}) {
    return truncated<Command>(meta.displayName, optionsWithMeta(meta, options));
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
  struct HandlerCacheEntry {
    CommandHandlerId handler;
    CommandKindId kind;
    CommandTypeToken commandType = nullptr;
    std::string name;
    bool preserved = false;
  };

  [[nodiscard]] static BytecodeCommandOptions optionsWithMeta(CommandMeta meta, BytecodeCommandOptions options) {
    if (!options.suffix) {
      options.suffix = meta.stableId;
    }
    return options;
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

    // Exact opcodes and ranges are both entries in the format's opcode table.
    // Reject overlap so no one has to remember which one would win.
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

  template <class Command>
  [[nodiscard]] std::pair<CommandHandlerId, CommandKindId> commandHandler(const std::string& kindName,
                                                                          std::string_view displayName) {
    const CommandTypeToken commandType = detail::commandTypeToken<Command>();
    if (const auto found = handlers_.find(kindName); found != handlers_.end()) {
      const HandlerCacheEntry& entry = found->second;
      // A range may map many opcodes to the same command type. Reusing the same
      // kind for a different type would make parsed commands point at the wrong code.
      if (entry.preserved || entry.commandType != commandType || entry.name != displayName) {
        throw std::logic_error("Bytecode command kind reused with incompatible handler");
      }
      return {entry.handler, entry.kind};
    }

    CommandHandlerId handler;
    CommandKindId kind;
    if (dialectBuilder_ != nullptr) {
      const auto registered = dialectBuilder_->template addCommand<Command>(kindName, displayName);
      handler = registered.handler;
      kind = registered.kind;
    } else {
      const CommandKind* foundKind = dialect_->kindForName(kindName);
      const CommandHandler* foundHandler = dialect_->template handlerForCommand<Command>();
      if (foundKind == nullptr || foundKind->name != displayName || foundHandler == nullptr) {
        throw std::logic_error("Bytecode command was not registered in its dialect");
      }
      handler = foundHandler->id;
      kind = foundKind->id;
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
      const auto registered = dialectBuilder_->addPreservedCommand(kindName, displayName);
      handler = registered.handler;
      kind = registered.kind;
    } else {
      const CommandKind* foundKind = dialect_->kindForName(kindName);
      const CommandHandler* foundHandler = dialect_->handlerForType(detail::preservedCommandTypeToken());
      if (foundKind == nullptr || foundKind->name != displayName || foundHandler == nullptr) {
        throw std::logic_error("Preserved bytecode command was not registered in its dialect");
      }
      handler = foundHandler->id;
      kind = foundKind->id;
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
