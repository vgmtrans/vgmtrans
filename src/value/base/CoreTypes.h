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

// Tiny ID wrappers make cross-asset references explicit without turning every
// model object into a heap-allocated node. invalidIdValue marks optional or
// not-yet-assigned IDs throughout the value model.
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

// SourceRange is the shared breadcrumb back to user-loaded or derived bytes.
// UI item trees, diagnostics, and source-backed commands all use this rather
// than storing format-specific address metadata in separate side channels.
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
