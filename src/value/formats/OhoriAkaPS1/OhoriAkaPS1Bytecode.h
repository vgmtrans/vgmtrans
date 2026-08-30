/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"

#include <array>
#include <optional>

namespace vgmtrans::formats::ohori_aka_ps1::bytecode {

inline constexpr u32 kMaximumCommands = 262144;
inline constexpr std::array<u8, 32> kControlParameterBytes{
    0, 1, 1, 1, 1, 1, 1, 2, 4, 1, 2, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

[[nodiscard]] inline std::optional<u32> variableEnd(core::ByteReader reader, u32 offset, u32 end) {
  if (offset >= end || !reader.has(offset, 1)) {
    return std::nullopt;
  }
  if ((reader.u8At(offset) & 0x80) == 0) return offset + 1;
  return offset + 1 < end ? std::optional<u32>{offset + 2} : std::nullopt;
}

[[nodiscard]] inline std::optional<u32> commandEnd(core::ByteReader reader, u32 offset, u32 end) {
  if (offset >= end || !reader.has(offset, 1)) {
    return std::nullopt;
  }
  const u8 status = reader.u8At(offset++);
  if (status < 0x80) {
    if (offset >= end) {
      return std::nullopt;
    }
    const u8 note = reader.u8At(offset++);
    if ((status & 0x60) == 0x40) {
      const auto next = variableEnd(reader, offset, end);
      if (!next) return std::nullopt;
      offset = *next;
    } else if ((status & 0x60) == 0x60) {
      ++offset;
    }
    if ((status & 0x1f) == 0x1f) {
      const auto next = variableEnd(reader, offset, end);
      if (!next) return std::nullopt;
      offset = *next;
    }
    if ((note & 0x80) != 0) ++offset;
  } else if ((status & 0x60) != 0x20) {
    offset += kControlParameterBytes[status & 0x1f];
    if ((status & 0x60) == 0x40) {
      const auto next = variableEnd(reader, offset, end);
      if (!next) return std::nullopt;
      offset = *next;
    } else if ((status & 0x60) == 0x60) {
      ++offset;
    }
  }
  return offset <= end ? std::optional<u32>{offset} : std::nullopt;
}

[[nodiscard]] inline bool isControl(u8 status, u8 command) {
  return status >= 0x80 && (status & 0x60) != 0x20 && (status & 0x1f) == command;
}

}  // namespace vgmtrans::formats::ohori_aka_ps1::bytecode
