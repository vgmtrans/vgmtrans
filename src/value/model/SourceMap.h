/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <optional>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vgmtrans::core {

struct SourceAnnotationIdTag;
using SourceAnnotationId = Id<SourceAnnotationIdTag>;

enum class SourceRole : u8 {
  Unknown,
  Source,
  Section,
  Header,
  Table,
  TableRow,
  Field,
  Pointer,
  Payload,
  Padding,
  DataBlock,
  Command,
  Opcode,
  Operand,
  Instrument,
  Sample,
  Diagnostic,
};

enum class SequenceSemantic : u8 {
  Unknown,
  Note,
  Rest,
  Wait,
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
};

using SourceValue = std::variant<std::monostate, bool, u64, s64, double, std::string>;

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
  Ascii,
  Enum,
};

struct SourceField {
  std::string name;
  SourceRange range;
  SourceValue value;
  SourceValueDisplay display = SourceValueDisplay::Default;
  std::optional<SequenceSemantic> sequenceSemantic;
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

enum class ObjectKind : u8 {
  Asset,
  Sequence,
  SequenceTrack,
  SequenceCommand,
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

namespace ObjectRefs {

[[nodiscard]] ObjectRef asset(AssetId asset);
[[nodiscard]] ObjectRef sequence(AssetId sequenceAsset);
[[nodiscard]] ObjectRef sequenceTrack(AssetId sequenceAsset, u32 trackIndex);
[[nodiscard]] ObjectRef sequenceCommand(AssetId sequenceAsset, u32 commandIndex);
[[nodiscard]] ObjectRef instrument(AssetId instrumentSetAsset, u32 instrumentIndex);
[[nodiscard]] ObjectRef sample(AssetId sampleSetAsset, u32 sampleIndex);
[[nodiscard]] ObjectRef misc(AssetId miscAsset);

}  // namespace ObjectRefs

using SourceTarget = std::variant<SourceRange, SourceAnnotationId, ObjectRef>;

struct SourceLink {
  SourceLinkRole role = SourceLinkRole::Related;
  SourceTarget target;
  std::string label;
};

struct SourceAnnotation {
  SourceAnnotationId id;
  SourceRange range;
  SourceRole role = SourceRole::Unknown;
  std::string label;
  std::optional<SequenceSemantic> sequenceSemantic;
  std::string localKind;
  std::optional<ObjectRef> owner;
  std::optional<SourceAnnotationId> parent;
  std::vector<SourceField> fields;
  std::vector<SourceLink> links;
};

class SourceMap {
public:
  SourceMap() = default;
  explicit SourceMap(std::vector<SourceAnnotation> annotations);

  [[nodiscard]] bool empty() const noexcept { return annotations_.empty(); }
  [[nodiscard]] std::span<const SourceAnnotation> annotations() const noexcept { return annotations_; }
  [[nodiscard]] const SourceAnnotation* find(SourceAnnotationId id) const;
  [[nodiscard]] const SourceAnnotation& get(SourceAnnotationId id) const;

  [[nodiscard]] std::vector<SourceAnnotationId> annotationsForSource(SourceId source) const;
  [[nodiscard]] std::vector<SourceAnnotationId> intersecting(SourceRange range) const;
  [[nodiscard]] std::vector<SourceAnnotationId> containing(SourceRange range) const;
  [[nodiscard]] std::vector<SourceAnnotationId> at(SourceId source, u64 offset) const;
  [[nodiscard]] std::vector<SourceAnnotationId> ownedBy(ObjectRef object) const;
  [[nodiscard]] std::vector<SourceAnnotationId> withRole(SourceId source, SourceRole role) const;
  [[nodiscard]] std::vector<SourceAnnotationId> withSequenceSemantic(SourceId source,
                                                                     SequenceSemantic semantic) const;
  [[nodiscard]] std::vector<SourceLink> linksFrom(SourceAnnotationId id) const;
  [[nodiscard]] std::vector<SourceAnnotationId> linksTo(const SourceTarget& target) const;

private:
  void buildIndexes();

  std::vector<SourceAnnotation> annotations_;
  std::unordered_map<u32, size_t> annotationsById_;
  std::unordered_map<u32, std::vector<SourceAnnotationId>> annotationsBySource_;
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
  AnnotationBuilder& kind(std::string_view localKindOverride);
  AnnotationBuilder& parent(SourceAnnotationId parent);
  AnnotationBuilder& owner(ObjectRef owner);
  AnnotationBuilder& sequenceSemantic(SequenceSemantic semantic);
  AnnotationBuilder& field(std::string_view name, SourceRange range, SourceValue value,
                           SourceValueDisplay display = SourceValueDisplay::Default);
  AnnotationBuilder& derived(std::string_view name, SourceValue value,
                             SourceValueDisplay display = SourceValueDisplay::Default);
  AnnotationBuilder& link(SourceLinkRole role, SourceTarget target, std::string_view label = {});

private:
  SourceMapBuilder* map_ = nullptr;
  SourceAnnotationId id_;
};

class SourceMapBuilder {
public:
  SourceMapBuilder() = default;
  explicit SourceMapBuilder(std::function<SourceAnnotationId()> nextId);

  [[nodiscard]] AnnotationBuilder source(std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder section(std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder header(std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder table(std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder row(std::string_view label, SourceRange range);
  [[nodiscard]] AnnotationBuilder field(std::string_view label, SourceRange range, SourceValue value);
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

namespace std {

template <>
struct hash<vgmtrans::core::ObjectRef> {
  size_t operator()(const vgmtrans::core::ObjectRef& ref) const noexcept {
    size_t seed = std::hash<int>{}(static_cast<int>(ref.kind));
    seed ^= std::hash<::u32>{}(ref.asset.value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<::u32>{}(ref.index0) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<::u32>{}(ref.index1) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
  }
};

}  // namespace std
