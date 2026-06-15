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

// IDs are stored as small values so assets can refer to each other without owning
// each other. invalidIdValue marks optional or not-yet-assigned IDs.
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

// A byte range inside a user-loaded or derived source. Parsed objects, diagnostics,
// and UI items use this to point back to the bytes they came from.
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

// Diagnostics are deliberately value objects so scanners/exporters can return
// partial results alongside warnings instead of failing the whole conversion.
struct Diagnostic {
  Severity severity = Severity::Info;
  std::string message;
  std::optional<SourceRange> range;
};

}  // namespace vgmtrans::core
