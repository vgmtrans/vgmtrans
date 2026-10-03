/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"

#include <algorithm>
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
  Region,
  Sample,
  InstrumentIndex,
  InstrumentProgram,
  Misc,
};

struct ObjectRef {
  ObjectKind kind = ObjectKind::Asset;
  AssetId asset;
  u32 index0 = 0;
  u32 index1 = 0;

  friend constexpr bool operator==(ObjectRef, ObjectRef) noexcept = default;
};

namespace ObjectRefs {

[[nodiscard]] inline ObjectRef asset(AssetId asset) {
  return ObjectRef{.kind = ObjectKind::Asset, .asset = asset};
}

[[nodiscard]] inline ObjectRef sequence(AssetId sequenceAsset) {
  return ObjectRef{.kind = ObjectKind::Sequence, .asset = sequenceAsset};
}

[[nodiscard]] inline ObjectRef sequenceTrack(AssetId sequenceAsset, u32 trackIndex) {
  return ObjectRef{.kind = ObjectKind::SequenceTrack, .asset = sequenceAsset, .index0 = trackIndex};
}

[[nodiscard]] inline ObjectRef instrument(AssetId soundBankAsset, u32 instrumentIndex) {
  return ObjectRef{.kind = ObjectKind::Instrument, .asset = soundBankAsset, .index0 = instrumentIndex};
}

[[nodiscard]] inline ObjectRef region(AssetId soundBankAsset, u32 instrumentIndex, u32 regionIndex) {
  return ObjectRef{
      .kind = ObjectKind::Region,
      .asset = soundBankAsset,
      .index0 = instrumentIndex,
      .index1 = regionIndex,
  };
}

[[nodiscard]] inline ObjectRef instrumentIndex(u32 instrumentIndex) {
  return ObjectRef{.kind = ObjectKind::InstrumentIndex, .index0 = instrumentIndex};
}

[[nodiscard]] inline ObjectRef instrumentProgram(u32 bank, u32 program) {
  return ObjectRef{.kind = ObjectKind::InstrumentProgram, .index0 = bank, .index1 = program};
}

[[nodiscard]] inline ObjectRef sample(AssetId sampleSetAsset, u32 sampleIndex) {
  return ObjectRef{.kind = ObjectKind::Sample, .asset = sampleSetAsset, .index0 = sampleIndex};
}

[[nodiscard]] inline ObjectRef misc(AssetId miscAsset) {
  return ObjectRef{.kind = ObjectKind::Misc, .asset = miscAsset};
}

}  // namespace ObjectRefs

// A byte range inside a user-loaded or derived source. Parsed objects, diagnostics,
// and UI items use this to point back to the bytes they came from.
struct SourceRange {
  SourceId source;
  u64 offset = 0;
  u64 size = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return source.valid(); }
  [[nodiscard]] constexpr u64 endOffset() const noexcept { return offset + size; }

  // Byte queries are half-open; zero-size ranges match their anchor offset.
  [[nodiscard]] constexpr bool containsOffset(SourceId querySource, u64 queryOffset) const noexcept {
    return valid() && source == querySource &&
           (size == 0 ? offset == queryOffset : offset <= queryOffset && queryOffset < endOffset());
  }

  // Expand to cover ranges from the same source, adopting the first valid
  // range. Invalid and foreign ranges do not contribute; zero-size anchors do.
  constexpr void include(SourceRange range) noexcept {
    if (!range.valid()) {
      return;
    }
    if (!valid()) {
      *this = range;
    } else if (source == range.source) {
      const u64 end = std::max(endOffset(), range.endOffset());
      offset = std::min(offset, range.offset);
      size = end - offset;
    }
  }

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
  // An invalid range means no source location was supplied.
  SourceRange range;
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

}  // namespace std
