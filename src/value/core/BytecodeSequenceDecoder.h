/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/SequenceDialect.h"
#include "value/core/Source.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
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

// Preserve source-driver commands that should remain visible/clickable in the
// source view even when they have no current performance effect.
struct PreservedBytecodeCommandSpec {
  u8 opcode = 0;
  std::string_view kind;
  std::string_view name;
  u32 operandBytes = 0;
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

[[nodiscard]] inline const CommandHandler& bytecodeHandlerForKind(const SequenceDialect& dialect,
                                                                  std::string_view kind) {
  const auto* handler = dialect.handlerForKind(kind);
  if (handler == nullptr) {
    throw std::logic_error("Sequence bytecode command was not registered in its dialect");
  }
  return *handler;
}

template <class Command>
[[nodiscard]] const CommandHandler& bytecodeHandlerFor(const SequenceDialect& dialect) {
  return bytecodeHandlerForKind(dialect, Command::kind);
}

[[nodiscard]] inline DecodedBytecodeCommand recordSizedPreservedBytecodeCommand(
    const SequenceDialect& dialect, ByteReader reader, u32 begin, u32 end, const PreservedBytecodeCommandSpec& spec) {
  const auto& handler = bytecodeHandlerForKind(dialect, spec.kind);
  const SourceRange range = reader.range(begin, end - begin);
  const auto bytes = reader.slice(begin, end - begin);
  std::vector<u8> ownedBytes{bytes.begin(), bytes.end()};
  std::vector<CommandOperand> operands;
  CommandReader commandReader{range, ownedBytes, &operands};
  if (spec.operandBytes > 0) {
    static_cast<void>(commandReader.rawBytes("bytes", spec.operandBytes));
  }
  if (!commandReader.done()) {
    throw std::invalid_argument("Preserved bytecode command left trailing source bytes");
  }
  return DecodedBytecodeCommand{
      .handler = handler.id,
      .kind = handler.kind,
      .range = range,
      .bytes = std::move(ownedBytes),
      .operands = std::move(operands),
  };
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

template <class TerminalCommand>
[[nodiscard]] DecodedBytecodeCommand recordPreservedBytecodeCommand(const SequenceDialect& dialect, ByteReader reader,
                                                                    u32 bytecodeEnd, u32 begin, u32 operandOffset,
                                                                    const PreservedBytecodeCommandSpec& spec) {
  if (!hasBytecodeBytes(reader, operandOffset, spec.operandBytes, bytecodeEnd)) {
    return terminalBytecodeCommand<TerminalCommand>(dialect, reader, begin, operandOffset);
  }
  auto decoded = recordSizedPreservedBytecodeCommand(dialect, reader, begin, operandOffset + spec.operandBytes, spec);
  decoded.flow.fallthrough = Address{operandOffset + spec.operandBytes};
  return decoded;
}

inline void appendDecodedBytecodeCommand(TrackProgramBuilder& builder, const DecodedBytecodeCommand& decoded,
                                         u32 offset) {
  builder.addDecoded(decoded.handler, decoded.kind, Address{offset}, decoded.range, decoded.bytes, decoded.operands);
}

// Common walkers own traversal mechanics and limits. Formats still own opcode
// decoding and control-flow classification for their source driver.
struct LinearBytecodeDecodePolicy {
  u32 maxCommands = 4096;
  bool stopAtVisitedOffset = true;
  bool followUnconditionalJumps = true;
};

template <class DecodeCommand>
[[nodiscard]] TrackProgram decodeLinearBytecodeTrack(ByteReader reader, u32 sourceTrackNumber, u32 startAddress,
                                                     LinearBytecodeDecodePolicy policy, DecodeCommand decodeCommand) {
  TrackProgram track{
      .id = TrackId{sourceTrackNumber},
      .sourceTrackNumber = sourceTrackNumber,
      .startAddress = Address{startAddress},
  };
  TrackProgramBuilder builder{track};
  std::set<u32> visitedOffsets;
  u32 offset = startAddress;

  while (reader.has(offset, 1) && track.commands.size() < policy.maxCommands) {
    if (policy.stopAtVisitedOffset && !visitedOffsets.insert(offset).second) {
      break;
    }

    const u32 begin = offset;
    auto decoded = decodeCommand(begin);
    const auto next = decoded.flow.fallthrough;
    const bool terminal = decoded.flow.terminal;
    const auto targets = decoded.flow.staticTargets;
    appendDecodedBytecodeCommand(builder, decoded, begin);

    if (terminal) {
      break;
    }
    if (next) {
      offset = next->value;
      continue;
    }
    if (policy.followUnconditionalJumps && targets.size() == 1) {
      offset = targets.front().value;
      continue;
    }
    break;
  }

  return track;
}

struct ReachableBytecodeDecodePolicy {
  u32 maxCommands = 262144;
};

template <class DecodeCommand>
[[nodiscard]] TrackProgram decodeReachableBytecodeBlocks(ByteReader reader, u32 bytecodeEnd, u32 startOffset,
                                                         u32 trackIndex, ReachableBytecodeDecodePolicy policy,
                                                         DecodeCommand decodeCommand) {
  TrackProgram track{
      .id = TrackId{trackIndex},
      .sourceTrackNumber = trackIndex,
      .startAddress = Address{startOffset},
  };
  TrackProgramBuilder builder{track};
  std::map<u32, DecodedBytecodeCommand> commandsByOffset;
  std::vector<u32> pendingBlocks{startOffset};
  size_t decodedCommands = 0;

  while (!pendingBlocks.empty() && decodedCommands < policy.maxCommands) {
    u32 offset = pendingBlocks.back();
    pendingBlocks.pop_back();
    while (hasBytecodeBytes(reader, offset, 1, bytecodeEnd) && !commandsByOffset.contains(offset) &&
           decodedCommands < policy.maxCommands) {
      auto decoded = decodeCommand(offset);
      ++decodedCommands;
      for (const Address target : decoded.flow.staticTargets) {
        if (target.value < bytecodeEnd && !commandsByOffset.contains(target.value)) {
          pendingBlocks.push_back(target.value);
        }
      }
      const auto next = decoded.flow.fallthrough;
      commandsByOffset.emplace(offset, std::move(decoded));
      if (!next) {
        break;
      }
      offset = next->value;
    }
  }

  for (const auto& [offset, decoded] : commandsByOffset) {
    appendDecodedBytecodeCommand(builder, decoded, offset);
  }
  return track;
}

}  // namespace vgmtrans::core
