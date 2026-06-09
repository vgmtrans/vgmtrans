/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"

#include <limits>
#include <optional>
#include <string>

namespace vgmtrans::core {

inline constexpr u32 invalidIdValue = std::numeric_limits<u32>::max();

struct SourceId {
  u32 value = invalidIdValue;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != invalidIdValue; }
  friend constexpr bool operator==(SourceId, SourceId) noexcept = default;
};

struct AssetId {
  u32 value = invalidIdValue;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != invalidIdValue; }
  friend constexpr bool operator==(AssetId, AssetId) noexcept = default;
};

struct CollectionId {
  u32 value = invalidIdValue;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != invalidIdValue; }
  friend constexpr bool operator==(CollectionId, CollectionId) noexcept = default;
};

struct ItemId {
  u32 value = invalidIdValue;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != invalidIdValue; }
  friend constexpr bool operator==(ItemId, ItemId) noexcept = default;
};

struct TrackId {
  u32 value = invalidIdValue;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != invalidIdValue; }
  friend constexpr bool operator==(TrackId, TrackId) noexcept = default;
};

struct SourceRange {
  SourceId source;
  u64 offset = 0;
  u64 size = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return source.valid(); }
  [[nodiscard]] constexpr u64 endOffset() const noexcept { return offset + size; }
};

enum class Severity {
  Info,
  Warning,
  Error,
};

struct Diagnostic {
  Severity severity = Severity::Info;
  std::string message;
  std::optional<SourceRange> range;
};

}  // namespace vgmtrans::core
