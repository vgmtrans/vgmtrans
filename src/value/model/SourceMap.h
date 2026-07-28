/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"
#include "value/model/SharedSequence.h"

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace vgmtrans::core {

class SessionState;

enum class SourceRole : u8 {
  Unknown,
  Source,
  Section,
  Header,
  Sequence,
  SequenceTrack,
  Table,
  TableEntry,
  Field,
  Pointer,
  Payload,
  Padding,
  DataBlock,
  Command,
  Opcode,
  Operand,
  InstrumentSet,
  Instrument,
  Region,
  SampleCollection,
  Sample,
};

enum class SequenceSemantic : u8 {
  Unknown,
  Note,
  Rest,
  Wait,
  // Common source-level classification; this does not prescribe an export format.
  Program,
  Level,
  Pan,
  Pitch,
  Tempo,
  Modulation,
  Portamento,
  Jump,
  Call,
  Return,
  End,
  Loop,
  Repeat,
  RepeatBreak,
  State,
  Meta,
  Unsupported,
  Instrument,
};

using SourceValue = std::variant<std::monostate, bool, u64, s64, double, std::string>;

[[nodiscard]] inline SourceValue makeSourceValue(SourceValue value) {
  return value;
}

[[nodiscard]] inline SourceValue makeSourceValue(bool value) {
  return SourceValue{value};
}

template <std::integral T>
  requires(!std::same_as<std::remove_cvref_t<T>, bool>)
[[nodiscard]] SourceValue makeSourceValue(T value) {
  if constexpr (std::is_signed_v<T>) {
    return SourceValue{static_cast<s64>(value)};
  } else {
    return SourceValue{static_cast<u64>(value)};
  }
}

template <std::floating_point T>
[[nodiscard]] SourceValue makeSourceValue(T value) {
  return SourceValue{static_cast<double>(value)};
}

[[nodiscard]] inline SourceValue makeSourceValue(std::string value) {
  return SourceValue{std::move(value)};
}

[[nodiscard]] inline SourceValue makeSourceValue(std::string_view value) {
  return SourceValue{std::string(value)};
}

enum class SourceValueDisplay : u8 {
  Default,
  Hex,
  Decimal,
  SignedDecimal,
  Boolean,
  Address,
  Percent,
  Cents,
  Decibels,
  MidiNote,
  BeatsPerMinute,
  Ascii,
  Enum,
};

struct SourceField {
  std::string name;
  SourceRange range;
  SourceValue value;
  SourceValueDisplay display = SourceValueDisplay::Default;
};

// The source description of one parsed structure. Keeping its span and fields
// together lets format code pass a record to synth builders without separately
// carrying an address, a length, and a parallel field list.
struct SourceRecord {
  SourceRange range;
  std::vector<SourceField> fields;
};

enum class SourceLinkRole : u8 {
  PointsTo,
  JumpTarget,
  CallTarget,
  LoopTarget,
  RepeatTarget,
  UsesInstrument,
  UsesSample,
  DerivedFrom,
  Related,
};

namespace ObjectRefs {

[[nodiscard]] ObjectRef asset(AssetId asset);
[[nodiscard]] ObjectRef sequence(AssetId sequenceAsset);
[[nodiscard]] ObjectRef sequenceTrack(AssetId sequenceAsset, u32 trackIndex);
[[nodiscard]] ObjectRef instrument(AssetId instrumentSetAsset, u32 instrumentIndex);
[[nodiscard]] ObjectRef region(AssetId instrumentSetAsset, u32 instrumentIndex, u32 regionIndex);
[[nodiscard]] ObjectRef instrumentIndex(u32 instrumentIndex);
[[nodiscard]] ObjectRef instrumentProgram(u32 bank, u32 program);
[[nodiscard]] ObjectRef sample(AssetId sampleSetAsset, u32 sampleIndex);
[[nodiscard]] ObjectRef sampleIndex(u32 sampleIndex);
[[nodiscard]] ObjectRef misc(AssetId miscAsset);

}  // namespace ObjectRefs

using SourceTarget = std::variant<SourceRange, SourceAnnotationId, ObjectRef>;

struct SourceLink {
  SourceLinkRole role = SourceLinkRole::Related;
  SourceTarget target;
  std::string label;
};

enum class SourceOutlinePolicy : u8 {
  Auto,
  Show,
  Hide,
};

enum class CommandPlaybackStatus : u8 {
  SourceOnly,
  NoOp,
  AffectsPlayback,
  AffectsControlFlow,
  StopsPlayback,
  Unsupported,
};

// Persistent source annotations are source-backed: every annotation stored in a
// session SourceMap must have a valid primary range. Derived/range-less facts
// should be fields on a source-backed annotation instead of standalone nodes.
struct SourceAnnotation {
  SourceAnnotationId id;
  SourceRange range;
  SourceRole role = SourceRole::Unknown;
  std::string label;
  std::string description;
  std::optional<SequenceSemantic> sequenceSemantic;
  std::optional<CommandPlaybackStatus> playbackStatus;
  std::string localKind;
  std::string detailKind;
  // Asset ownership is explicit at graph roots and inherited by structural
  // descendants. Scan validation rejects parents that cross asset boundaries.
  std::optional<ObjectRef> owner;
  std::optional<SourceAnnotationId> parent;
  SourceOutlinePolicy outline = SourceOutlinePolicy::Auto;
  std::vector<SourceField> fields;
  std::vector<SourceLink> links;
};

class SourceMap {
public:
  SourceMap();
  explicit SourceMap(std::vector<SourceAnnotation> annotations);

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] const SharedSequence<SourceAnnotation>& annotations() const noexcept;
  [[nodiscard]] const SourceAnnotation* find(SourceAnnotationId id) const;
  [[nodiscard]] const SourceAnnotation& get(SourceAnnotationId id) const;

  [[nodiscard]] std::vector<SourceAnnotationId> annotationsForSource(SourceId source) const;
  [[nodiscard]] std::vector<SourceAnnotationId> intersecting(SourceRange range) const;
  [[nodiscard]] std::vector<SourceAnnotationId> containing(SourceRange range) const;
  [[nodiscard]] std::vector<SourceAnnotationId> at(SourceId source, u64 offset) const;
  [[nodiscard]] std::vector<SourceAnnotationId> ownedBy(ObjectRef object) const;
  [[nodiscard]] std::optional<AssetId> assetOwner(SourceAnnotationId id) const;
  [[nodiscard]] std::vector<SourceAnnotationId> annotationsForAsset(AssetId asset) const;
  [[nodiscard]] std::vector<SourceAnnotationId> childrenOf(SourceAnnotationId parent) const;
  [[nodiscard]] std::vector<SourceAnnotationId> withRole(SourceId source, SourceRole role) const;
  [[nodiscard]] std::vector<SourceAnnotationId> withSequenceSemantic(SourceId source, SequenceSemantic semantic) const;
  [[nodiscard]] std::vector<SourceLink> linksFrom(SourceAnnotationId id) const;
  [[nodiscard]] std::vector<SourceAnnotationId> linksTo(const SourceTarget& target) const;

private:
  friend class SessionState;

  struct Index {
    explicit Index(const std::vector<SourceAnnotation>& annotations);

    std::unordered_map<u32, size_t> annotationsById;
    std::unordered_map<u32, std::vector<SourceAnnotationId>> annotationsBySource;
    std::unordered_map<u32, std::vector<SourceAnnotationId>> annotationsByParent;
  };

  struct Part {
    std::shared_ptr<const std::vector<SourceAnnotation>> annotations;
    std::shared_ptr<const Index> index;
  };

  struct Storage {
    explicit Storage(std::vector<Part> parts);

    std::vector<Part> parts;
    SharedSequence<SourceAnnotation> annotations;
  };

  explicit SourceMap(std::shared_ptr<const Storage> storage);
  [[nodiscard]] static SourceMap join(std::span<const SourceMap> maps);
  [[nodiscard]] static std::shared_ptr<const Storage> emptyStorage();

  std::shared_ptr<const Storage> storage_;
};

class SourceMapBuilder;

class AnnotationBuilder {
public:
  AnnotationBuilder() = default;
  AnnotationBuilder(SourceMapBuilder& map, SourceAnnotationId id);

  [[nodiscard]] SourceAnnotationId id() const noexcept { return id_; }

  AnnotationBuilder& role(SourceRole role);
  AnnotationBuilder& range(SourceRange range);
  AnnotationBuilder& label(std::string_view label);
  AnnotationBuilder& description(std::string_view description);
  AnnotationBuilder& kind(std::string_view localKindOverride);
  AnnotationBuilder& detailKind(std::string_view detailKind);
  AnnotationBuilder& parent(SourceAnnotationId parent);
  AnnotationBuilder& owner(ObjectRef owner);
  AnnotationBuilder& outline(SourceOutlinePolicy policy);
  AnnotationBuilder& sequenceSemantic(SequenceSemantic semantic);
  AnnotationBuilder& playbackStatus(CommandPlaybackStatus status);
  AnnotationBuilder& field(std::string_view name, SourceRange range, SourceValue value,
                           SourceValueDisplay display = SourceValueDisplay::Default);
  AnnotationBuilder& fields(std::span<const SourceField> fields);
  template <class T>
  AnnotationBuilder& field(std::string_view name, SourceRange range, T&& value,
                           SourceValueDisplay display = SourceValueDisplay::Default) {
    return field(name, range, makeSourceValue(std::forward<T>(value)), display);
  }
  template <class T>
  AnnotationBuilder& field(std::string_view name, const RangedValue<T>& value,
                           SourceValueDisplay display = SourceValueDisplay::Default) {
    return value ? field(name, value.range, value.value, display) : *this;
  }
  AnnotationBuilder& derived(std::string_view name, SourceValue value,
                             SourceValueDisplay display = SourceValueDisplay::Default);
  template <class T>
  AnnotationBuilder& derived(std::string_view name, T&& value,
                             SourceValueDisplay display = SourceValueDisplay::Default) {
    return derived(name, makeSourceValue(std::forward<T>(value)), display);
  }
  AnnotationBuilder& link(SourceLinkRole role, SourceTarget target, std::string_view label = {});

private:
  [[nodiscard]] SourceAnnotation* annotation() const;

  SourceMapBuilder* map_ = nullptr;
  SourceAnnotationId id_;
};

class SourceMapBuilder {
public:
  SourceMapBuilder() = default;
  explicit SourceMapBuilder(std::function<SourceAnnotationId()> nextId);

  [[nodiscard]] AnnotationBuilder source(std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder annotation(SourceRole role, std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder section(std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder header(std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder table(std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder entry(std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder field(std::string_view label, SourceRange range, SourceValue value);
  template <class T>
  [[nodiscard]] AnnotationBuilder field(std::string_view label, SourceRange range, T&& value) {
    return field(label, range, makeSourceValue(std::forward<T>(value)));
  }
  [[nodiscard]] AnnotationBuilder pointer(std::string_view label, SourceRange range, SourceTarget target);
  [[nodiscard]] AnnotationBuilder command(std::string_view label, SourceRange range,
                                          SequenceSemantic semantic = SequenceSemantic::Unknown);

  [[nodiscard]] SourceMap finish();

private:
  friend class AnnotationBuilder;

  [[nodiscard]] SourceAnnotationId allocateId();
  [[nodiscard]] AnnotationBuilder add(SourceRole role, std::string_view label, SourceRange range);
  [[nodiscard]] SourceAnnotation* annotation(SourceAnnotationId id);

  std::function<SourceAnnotationId()> nextId_;
  u32 nextLocalId_ = 0;
  std::vector<SourceAnnotation> annotations_;
  std::unordered_map<u32, size_t> annotationsById_;
};

[[nodiscard]] std::string sourceLocalKind(std::string_view label);

}  // namespace vgmtrans::core
