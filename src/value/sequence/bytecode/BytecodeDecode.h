/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/bytecode/BytecodeTable.h"

#include <algorithm>
#include <concepts>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace vgmtrans::core {

template <class Command>
struct ParsedBytecodeCommand {
  DecodedBytecodeCommand decoded;
  Command command;
};

[[nodiscard]] inline DecodedBytecodeCommand recordSizedPreservedBytecodeCommand(const BytecodeCommandSpec& spec,
                                                                                ByteReader reader, u32 begin, u32 end) {
  const SourceRange range = reader.range(begin, end - begin);
  const auto bytes = reader.slice(begin, end - begin);
  std::vector<u8> ownedBytes{bytes.begin(), bytes.end()};
  std::vector<CommandOperand> operands;
  CommandReader commandReader{range, ownedBytes, &operands};
  const auto operandBytes = static_cast<u32>(end - begin - 1);
  if (operandBytes > 0) {
    // Keep unknown operands as raw bytes instead of inventing names or meanings.
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
  // Simple branches can be declared in the opcode map. More unusual branch rules
  // live on the command type itself as decodeFlow().
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
  // Variable-length commands do not know their next address until parsing is done.
  context.commandEnd = begin + static_cast<u32>(decoded.bytes.size());
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
  decoded.flow = DecodeFlow::call(parsed->command.*Target, Address{begin + static_cast<u32>(decoded.bytes.size())});
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
  if (decoded.commandKind) {
    builder.addDecoded(decoded.handler, *decoded.commandKind, Address{offset}, decoded.range, decoded.bytes,
                       decoded.operands);
    return;
  }
  builder.addDecoded(decoded.handler, decoded.kind, Address{offset}, decoded.range, decoded.bytes, decoded.operands);
}

}  // namespace vgmtrans::core
