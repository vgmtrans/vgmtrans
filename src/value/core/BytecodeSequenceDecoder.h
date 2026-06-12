/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/SequenceDialect.h"
#include "value/core/Source.h"

#include <algorithm>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::core {

// Parse mixins for common source-driver command shapes. They keep format command
// structs compact while still recording named operands for the source view.
template <class Derived>
struct NoOperands {
  static Derived parse(CommandReader&) { return {}; }
};

template <class Derived>
struct U8Operand {
  u8 raw = 0;

  static Derived parse(CommandReader& in) {
    Derived result;
    result.raw = in.u8(Derived::operandName);
    return result;
  }
};

template <class Derived>
struct S8Operand {
  s8 raw = 0;

  static Derived parse(CommandReader& in) {
    Derived result;
    result.raw = in.s8(Derived::operandName);
    return result;
  }
};

template <class Derived>
struct Be16Operand {
  u16 raw = 0;

  static Derived parse(CommandReader& in) {
    Derived result;
    result.raw = in.be16(Derived::operandName);
    return result;
  }
};

template <class Derived, size_t OperandBytes>
struct RawBytesOperand {
  static constexpr u32 operandBytes = OperandBytes;
  std::string bytes;

  static Derived parse(CommandReader& in) {
    Derived result;
    if constexpr (OperandBytes > 0) {
      result.bytes = in.rawBytes("bytes", OperandBytes);
    }
    return result;
  }
};

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

[[nodiscard]] inline bool hasBytecodeBytes(ByteReader reader, u32 offset, u32 size, u32 end) {
  return offset <= end && size <= end - offset && reader.has(offset, size);
}

template <class Command>
[[nodiscard]] const CommandHandler& bytecodeHandlerFor(const SequenceDialect& dialect) {
  const auto* handler = dialect.handlerForKind(Command::kind);
  if (handler == nullptr) {
    throw std::logic_error("Sequence bytecode command was not registered in its dialect");
  }
  return *handler;
}

template <class Command>
[[nodiscard]] DecodedBytecodeCommand recordSizedBytecodeCommand(const SequenceDialect& dialect, ByteReader reader,
                                                                u32 begin, u32 end) {
  const auto& handler = bytecodeHandlerFor<Command>(dialect);
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
      .handler = handler.id,
      .kind = handler.kind,
      .range = range,
      .bytes = std::move(ownedBytes),
      .operands = std::move(operands),
  };
}

// Parse from the available bytecode span, then shrink the saved source bytes to
// exactly the amount consumed by Command::parse().
template <class Command>
[[nodiscard]] std::optional<ParsedBytecodeCommand<Command>> parseBytecodeCommand(const SequenceDialect& dialect,
                                                                                 ByteReader reader, u32 begin,
                                                                                 u32 end) {
  if (!hasBytecodeBytes(reader, begin, 1, end)) {
    return std::nullopt;
  }

  const auto boundedEnd = static_cast<u32>(std::min<size_t>(reader.size(), end));
  if (begin >= boundedEnd) {
    return std::nullopt;
  }

  const auto& handler = bytecodeHandlerFor<Command>(dialect);
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
              .handler = handler.id,
              .kind = handler.kind,
              .range = reader.range(begin, commandSize),
              .bytes = std::move(ownedBytes),
              .operands = std::move(operands),
          },
      .command = command,
  };
}

template <class TerminalCommand>
[[nodiscard]] DecodedBytecodeCommand terminalBytecodeCommand(const SequenceDialect& dialect, ByteReader reader,
                                                             u32 begin, u32 end) {
  auto decoded = recordSizedBytecodeCommand<TerminalCommand>(dialect, reader, begin, end);
  decoded.flow.terminal = true;
  return decoded;
}

template <class TerminalCommand>
[[nodiscard]] DecodedBytecodeCommand truncatedBytecodeCommand(const SequenceDialect& dialect, ByteReader reader,
                                                              u32 begin, u32 end) {
  const auto boundedEnd = static_cast<u32>(std::min<size_t>(reader.size(), end));
  return terminalBytecodeCommand<TerminalCommand>(dialect, reader, begin, std::min(begin + 1, boundedEnd));
}

template <class Command, class TerminalCommand>
[[nodiscard]] DecodedBytecodeCommand recordAutoBytecodeCommand(const SequenceDialect& dialect, ByteReader reader,
                                                               u32 begin, u32 end) {
  auto parsed = parseBytecodeCommand<Command>(dialect, reader, begin, end);
  if (!parsed) {
    return truncatedBytecodeCommand<TerminalCommand>(dialect, reader, begin, end);
  }
  return std::move(parsed->decoded);
}

template <class Command, class TerminalCommand>
[[nodiscard]] DecodedBytecodeCommand recordAutoFallthroughBytecodeCommand(const SequenceDialect& dialect,
                                                                          ByteReader reader, u32 begin, u32 end) {
  auto decoded = recordAutoBytecodeCommand<Command, TerminalCommand>(dialect, reader, begin, end);
  if (!decoded.flow.terminal) {
    decoded.flow.fallthrough = Address{static_cast<u32>(decoded.range.endOffset())};
  }
  return decoded;
}

// Preserve an ignored source-driver command as its own typed command. It has no
// performance effect, but the UI can still show its bytes and operand range.
template <class IgnoredCommand, class TerminalCommand>
[[nodiscard]] DecodedBytecodeCommand recordIgnoredBytecodeCommand(const SequenceDialect& dialect, ByteReader reader,
                                                                  u32 bytecodeEnd, u32 begin, u32 operandOffset,
                                                                  u32 operandBytes) {
  if (!hasBytecodeBytes(reader, operandOffset, operandBytes, bytecodeEnd)) {
    return terminalBytecodeCommand<TerminalCommand>(dialect, reader, begin, operandOffset);
  }
  auto decoded = recordSizedBytecodeCommand<IgnoredCommand>(dialect, reader, begin, operandOffset + operandBytes);
  decoded.flow.fallthrough = Address{operandOffset + operandBytes};
  return decoded;
}

inline void appendDecodedBytecodeCommand(TrackProgramBuilder& builder, const DecodedBytecodeCommand& decoded,
                                         u32 offset) {
  builder.addDecoded(decoded.handler, decoded.kind, Address{offset}, decoded.range, decoded.bytes, decoded.operands);
}

}  // namespace vgmtrans::core
