/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace vgmtrans::core {

// IDs are stored as small values so assets can refer to each other without owning
// each other. invalidIdValue marks optional or not-yet-assigned IDs.
inline constexpr u32 invalidIdValue = std::numeric_limits<u32>::max();

template <class Tag>
struct Id {
  u32 value = invalidIdValue;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != invalidIdValue; }
  friend constexpr bool operator==(Id, Id) noexcept = default;
};

struct SourceIdTag;
using SourceId = Id<SourceIdTag>;

struct SourceAnnotationIdTag;
using SourceAnnotationId = Id<SourceAnnotationIdTag>;

struct AssetIdTag;
using AssetId = Id<AssetIdTag>;

struct CollectionIdTag;
using CollectionId = Id<CollectionIdTag>;

struct TrackIdTag;
using TrackId = Id<TrackIdTag>;

struct CommandIdTag;
using CommandId = Id<CommandIdTag>;

enum class ObjectKind : u8 {
  Asset,
  Sequence,
  SequenceTrack,
  Instrument,
  Sample,
  Misc,
};

struct ObjectRef {
  ObjectKind kind = ObjectKind::Asset;
  AssetId asset;
  u32 index0 = 0;
  u32 index1 = 0;

  friend constexpr bool operator==(ObjectRef, ObjectRef) noexcept = default;
};

// A byte range inside a user-loaded or derived source. Parsed objects, diagnostics,
// and UI items use this to point back to the bytes they came from.
struct SourceRange {
  SourceId source;
  u64 offset = 0;
  u64 size = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return source.valid(); }
  [[nodiscard]] constexpr u64 endOffset() const noexcept { return offset + size; }
  friend constexpr bool operator==(SourceRange, SourceRange) noexcept = default;
};

template <class T>
struct RangedValue {
  T value{};
  SourceRange range;
  bool valid = false;

  RangedValue() = default;
  RangedValue(T parsedValue, SourceRange parsedRange)
      : value(std::move(parsedValue)), range(parsedRange), valid(true) {
  }

  [[nodiscard]] explicit operator bool() const noexcept { return valid; }
  [[nodiscard]] const T& operator*() const noexcept { return value; }
  [[nodiscard]] T& operator*() noexcept { return value; }
  [[nodiscard]] operator T() const noexcept { return value; }
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
  std::string code;
  std::string message;
  std::optional<SourceRange> range;
  std::optional<SourceAnnotationId> annotation;
  std::optional<ObjectRef> object;
};

}  // namespace vgmtrans::core

namespace std {

template <class Tag>
struct hash<vgmtrans::core::Id<Tag>> {
  std::size_t operator()(vgmtrans::core::Id<Tag> id) const noexcept {
    return std::hash<::u32>{}(id.value);
  }
};

template <>
struct hash<vgmtrans::core::ObjectRef> {
  std::size_t operator()(const vgmtrans::core::ObjectRef& ref) const noexcept {
    size_t seed = std::hash<int>{}(static_cast<int>(ref.kind));
    seed ^= std::hash<::u32>{}(ref.asset.value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<::u32>{}(ref.index0) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<::u32>{}(ref.index1) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
  }
};

}  // namespace std
