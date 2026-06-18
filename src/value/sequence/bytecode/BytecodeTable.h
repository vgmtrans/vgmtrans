/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/sequence/SequenceProgram.h"

#include <algorithm>
#include <any>
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

class SourceMapBuilder;

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

struct BytecodeDecodeContext {
  // bytecodeEnd is the first offset the decoder must not read. sequenceEnd can be
  // smaller when a driver stores several sequences in one larger byte buffer.
  u32 bytecodeEnd = std::numeric_limits<u32>::max();
  u32 sequenceOffset = 0;
  u32 sequenceEnd = std::numeric_limits<u32>::max();
  u32 commandEnd = 0;
  const std::any* dialectContext = nullptr;
  SourceMapBuilder* sourceMap = nullptr;
  std::vector<Diagnostic>* diagnostics = nullptr;
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
  std::optional<CommandPlaybackStatus> playbackStatus;
};

// Use this when a command needs an explicit stable ID instead of deriving one
// from the display name shown in the source view.
struct CommandMeta {
  std::string_view stableId;
  std::string_view displayName;
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

struct BytecodeRangeSpec {
  u8 first = 0;
  u8 last = 0;
  BytecodeCommandSpec spec;
};

[[nodiscard]] constexpr CommandMeta commandMeta(std::string_view stableId, std::string_view displayName) {
  return CommandMeta{
      .stableId = stableId,
      .displayName = displayName,
  };
}

[[nodiscard]] constexpr FixedOperandByteCount operandBytes(u32 value) {
  return FixedOperandByteCount{.value = value};
}

// Use this for known opcodes that should appear in the parsed source view even
// when they do not affect playback yet.
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

[[nodiscard]] constexpr PreservedBytecodeCommand preservedOpcode(u8 opcode, CommandMeta meta,
                                                                 FixedOperandByteCount operandBytes = {},
                                                                 BytecodeCommandOptions options = {}) {
  if (!options.suffix) {
    options.suffix = meta.stableId;
  }
  return preservedOpcode(opcode, meta.displayName, operandBytes, options);
}

[[nodiscard]] inline bool hasBytecodeBytes(ByteReader reader, u32 offset, u32 size, u32 end) {
  return offset <= end && size <= end - offset && reader.has(offset, size);
}

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
    // Exact opcodes are checked before ranges, but finish() rejects overlap so an
    // accidental duplicate cannot change behavior silently.
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

}  // namespace vgmtrans::core
