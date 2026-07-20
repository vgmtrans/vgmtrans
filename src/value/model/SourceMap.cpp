/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/SourceMap.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] bool rangeContains(SourceRange outer, SourceRange inner) {
  if (!outer.valid() || !inner.valid() || outer.source != inner.source) {
    return false;
  }
  if (inner.offset < outer.offset) {
    return false;
  }
  if (inner.offset > outer.endOffset()) {
    return false;
  }
  return inner.size <= outer.endOffset() - inner.offset;
}

[[nodiscard]] bool rangeContainsOffset(SourceRange range, SourceId source, u64 offset) {
  if (!range.valid() || range.source != source) {
    return false;
  }
  if (range.size == 0) {
    return range.offset == offset;
  }
  return range.offset <= offset && offset < range.endOffset();
}

[[nodiscard]] bool rangesIntersect(SourceRange lhs, SourceRange rhs) {
  if (!lhs.valid() || !rhs.valid() || lhs.source != rhs.source) {
    return false;
  }
  if (lhs.size == 0 || rhs.size == 0) {
    return rangeContainsOffset(lhs, rhs.source, rhs.offset) || rangeContainsOffset(rhs, lhs.source, lhs.offset);
  }
  return lhs.offset < rhs.endOffset() && rhs.offset < lhs.endOffset();
}

[[nodiscard]] std::vector<SourceAnnotationId> idsFromAnnotations(std::span<const SourceAnnotation> annotations,
                                                                 auto predicate) {
  std::vector<SourceAnnotationId> ids;
  for (const auto& annotation : annotations) {
    if (predicate(annotation)) {
      ids.push_back(annotation.id);
    }
  }
  return ids;
}

}  // namespace

ObjectRef ObjectRefs::asset(AssetId asset) {
  return ObjectRef{.kind = ObjectKind::Asset, .asset = asset};
}

ObjectRef ObjectRefs::sequence(AssetId sequenceAsset) {
  return ObjectRef{.kind = ObjectKind::Sequence, .asset = sequenceAsset};
}

ObjectRef ObjectRefs::sequenceTrack(AssetId sequenceAsset, u32 trackIndex) {
  return ObjectRef{.kind = ObjectKind::SequenceTrack, .asset = sequenceAsset, .index0 = trackIndex};
}

ObjectRef ObjectRefs::instrument(AssetId instrumentSetAsset, u32 instrumentIndex) {
  return ObjectRef{.kind = ObjectKind::Instrument, .asset = instrumentSetAsset, .index0 = instrumentIndex};
}

ObjectRef ObjectRefs::region(AssetId instrumentSetAsset, u32 instrumentIndex, u32 regionIndex) {
  return ObjectRef{
      .kind = ObjectKind::Region,
      .asset = instrumentSetAsset,
      .index0 = instrumentIndex,
      .index1 = regionIndex,
  };
}

ObjectRef ObjectRefs::instrumentIndex(u32 instrumentIndex) {
  return ObjectRef{.kind = ObjectKind::Instrument, .index0 = instrumentIndex};
}

ObjectRef ObjectRefs::instrumentProgram(u32 bank, u32 program) {
  return ObjectRef{.kind = ObjectKind::Instrument, .index0 = bank, .index1 = program};
}

ObjectRef ObjectRefs::sample(AssetId sampleSetAsset, u32 sampleIndex) {
  return ObjectRef{.kind = ObjectKind::Sample, .asset = sampleSetAsset, .index0 = sampleIndex};
}

ObjectRef ObjectRefs::sampleIndex(u32 sampleIndex) {
  return ObjectRef{.kind = ObjectKind::Sample, .index0 = sampleIndex};
}

ObjectRef ObjectRefs::misc(AssetId miscAsset) {
  return ObjectRef{.kind = ObjectKind::Misc, .asset = miscAsset};
}

SourceMap::SourceMap(std::vector<SourceAnnotation> annotations) : annotations_(std::move(annotations)) {
  buildIndexes();
}

const SourceAnnotation* SourceMap::find(SourceAnnotationId id) const {
  const auto found = annotationsById_.find(id.value);
  if (found == annotationsById_.end() || found->second >= annotations_.size()) {
    return nullptr;
  }
  const auto& annotation = annotations_[found->second];
  return annotation.id == id ? &annotation : nullptr;
}

const SourceAnnotation& SourceMap::get(SourceAnnotationId id) const {
  const auto* annotation = find(id);
  if (annotation == nullptr) {
    throw std::out_of_range("SourceAnnotationId was not found in SourceMap");
  }
  return *annotation;
}

std::vector<SourceAnnotationId> SourceMap::annotationsForSource(SourceId source) const {
  const auto found = annotationsBySource_.find(source.value);
  if (found == annotationsBySource_.end()) {
    return {};
  }
  return found->second;
}

std::vector<SourceAnnotationId> SourceMap::intersecting(SourceRange range) const {
  return idsFromAnnotations(
      annotations_, [&](const SourceAnnotation& annotation) { return rangesIntersect(annotation.range, range); });
}

std::vector<SourceAnnotationId> SourceMap::containing(SourceRange range) const {
  return idsFromAnnotations(annotations_,
                            [&](const SourceAnnotation& annotation) { return rangeContains(annotation.range, range); });
}

std::vector<SourceAnnotationId> SourceMap::at(SourceId source, u64 offset) const {
  return idsFromAnnotations(annotations_, [&](const SourceAnnotation& annotation) {
    return rangeContainsOffset(annotation.range, source, offset);
  });
}

std::vector<SourceAnnotationId> SourceMap::ownedBy(ObjectRef object) const {
  return idsFromAnnotations(annotations_, [&](const SourceAnnotation& annotation) {
    return annotation.owner && *annotation.owner == object;
  });
}

std::vector<SourceAnnotationId> SourceMap::childrenOf(SourceAnnotationId parent) const {
  const auto found = annotationsByParent_.find(parent.value);
  if (found == annotationsByParent_.end()) {
    return {};
  }
  return found->second;
}

std::vector<SourceAnnotationId> SourceMap::withRole(SourceId source, SourceRole role) const {
  return idsFromAnnotations(annotations_, [&](const SourceAnnotation& annotation) {
    return annotation.range.source == source && annotation.role == role;
  });
}

std::vector<SourceAnnotationId> SourceMap::withSequenceSemantic(SourceId source, SequenceSemantic semantic) const {
  return idsFromAnnotations(annotations_, [&](const SourceAnnotation& annotation) {
    return annotation.range.source == source && annotation.sequenceSemantic == semantic;
  });
}

std::vector<SourceLink> SourceMap::linksFrom(SourceAnnotationId id) const {
  const auto* annotation = find(id);
  return annotation != nullptr ? annotation->links : std::vector<SourceLink>{};
}

std::vector<SourceAnnotationId> SourceMap::linksTo(const SourceTarget& target) const {
  return idsFromAnnotations(annotations_, [&](const SourceAnnotation& annotation) {
    return std::ranges::any_of(annotation.links, [&](const SourceLink& link) { return link.target == target; });
  });
}

void SourceMap::buildIndexes() {
  annotationsById_.reserve(annotations_.size());
  for (size_t i = 0; i < annotations_.size(); ++i) {
    const auto id = annotations_[i].id;
    if (id.valid()) {
      const auto [_, inserted] = annotationsById_.emplace(id.value, i);
      if (!inserted) {
        throw std::logic_error("Duplicate SourceAnnotationId in SourceMap");
      }
    }
    if (annotations_[i].range.source.valid()) {
      annotationsBySource_[annotations_[i].range.source.value].push_back(id);
    }
    if (annotations_[i].parent && annotations_[i].parent->valid()) {
      annotationsByParent_[annotations_[i].parent->value].push_back(id);
    }
  }
}

AnnotationBuilder::AnnotationBuilder(SourceMapBuilder& map, SourceAnnotationId id) : map_(&map), id_(id) {
}

SourceAnnotation* AnnotationBuilder::annotation() const {
  return map_ != nullptr ? map_->annotation(id_) : nullptr;
}

AnnotationBuilder& AnnotationBuilder::role(SourceRole role) {
  if (auto* found = annotation()) {
    found->role = role;
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::range(SourceRange range) {
  if (auto* found = annotation()) {
    found->range = range;
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::label(std::string_view label) {
  if (auto* found = annotation()) {
    found->label = std::string(label);
    if (found->localKind.empty()) {
      found->localKind = sourceLocalKind(label);
    }
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::description(std::string_view description) {
  if (auto* found = annotation()) {
    found->description = std::string(description);
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::kind(std::string_view localKindOverride) {
  if (auto* found = annotation()) {
    found->localKind = std::string(localKindOverride);
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::detailKind(std::string_view detailKind) {
  if (auto* found = annotation()) {
    found->detailKind = std::string(detailKind);
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::parent(SourceAnnotationId parent) {
  if (auto* found = annotation()) {
    found->parent = parent;
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::owner(ObjectRef owner) {
  if (auto* found = annotation()) {
    found->owner = owner;
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::outline(SourceOutlinePolicy policy) {
  if (auto* found = annotation()) {
    found->outline = policy;
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::sequenceSemantic(SequenceSemantic semantic) {
  if (auto* found = annotation()) {
    found->sequenceSemantic = semantic;
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::playbackStatus(CommandPlaybackStatus status) {
  if (auto* found = annotation()) {
    found->playbackStatus = status;
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::field(std::string_view name, SourceRange range, SourceValue value,
                                            SourceValueDisplay display) {
  if (auto* found = annotation()) {
    found->fields.push_back(SourceField{
        .name = std::string(name),
        .range = range,
        .value = std::move(value),
        .display = display,
    });
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::fields(std::span<const SourceField> fields) {
  if (auto* found = annotation()) {
    found->fields.insert(found->fields.end(), fields.begin(), fields.end());
  }
  return *this;
}

AnnotationBuilder& AnnotationBuilder::derived(std::string_view name, SourceValue value, SourceValueDisplay display) {
  return field(name, SourceRange{}, std::move(value), display);
}

AnnotationBuilder& AnnotationBuilder::link(SourceLinkRole role, SourceTarget target, std::string_view label) {
  if (auto* found = annotation()) {
    const auto duplicate = std::ranges::find_if(found->links, [&](const SourceLink& link) {
      return link.role == role && link.target == target && link.label == label;
    });
    if (duplicate == found->links.end()) {
      found->links.push_back(SourceLink{
          .role = role,
          .target = std::move(target),
          .label = std::string(label),
      });
    }
  }
  return *this;
}

SourceMapBuilder::SourceMapBuilder(std::function<SourceAnnotationId()> nextId) : nextId_(std::move(nextId)) {
}

AnnotationBuilder SourceMapBuilder::source(std::string_view label, SourceRange range) {
  return add(SourceRole::Source, label, range);
}

AnnotationBuilder SourceMapBuilder::annotation(SourceRole role, std::string_view label, SourceRange range) {
  return add(role, label, range);
}

AnnotationBuilder SourceMapBuilder::section(std::string_view label, SourceRange range) {
  return add(SourceRole::Section, label, range);
}

AnnotationBuilder SourceMapBuilder::header(std::string_view label, SourceRange range) {
  return add(SourceRole::Header, label, range);
}

AnnotationBuilder SourceMapBuilder::table(std::string_view label, SourceRange range) {
  return add(SourceRole::Table, label, range);
}

AnnotationBuilder SourceMapBuilder::entry(std::string_view label, SourceRange range) {
  return add(SourceRole::TableEntry, label, range);
}

AnnotationBuilder SourceMapBuilder::field(std::string_view label, SourceRange range, SourceValue value) {
  auto annotation = add(SourceRole::Field, label, range);
  annotation.field(label, range, std::move(value));
  return annotation;
}

AnnotationBuilder SourceMapBuilder::pointer(std::string_view label, SourceRange range, SourceTarget target) {
  auto annotation = add(SourceRole::Pointer, label, range);
  annotation.link(SourceLinkRole::PointsTo, std::move(target));
  return annotation;
}

AnnotationBuilder SourceMapBuilder::command(std::string_view label, SourceRange range, SequenceSemantic semantic) {
  auto annotation = add(SourceRole::Command, label, range);
  if (semantic != SequenceSemantic::Unknown) {
    annotation.sequenceSemantic(semantic);
  }
  return annotation;
}

SourceMap SourceMapBuilder::finish() {
  return SourceMap{std::move(annotations_)};
}

SourceAnnotationId SourceMapBuilder::allocateId() {
  return nextId_ ? nextId_() : SourceAnnotationId{nextLocalId_++};
}

AnnotationBuilder SourceMapBuilder::add(SourceRole role, std::string_view label, SourceRange range) {
  const auto id = allocateId();
  if (id.valid() && annotationsById_.contains(id.value)) {
    throw std::logic_error("Duplicate SourceAnnotationId in SourceMapBuilder");
  }
  const auto index = annotations_.size();
  annotations_.push_back(SourceAnnotation{
      .id = id,
      .range = range,
      .role = role,
      .label = std::string(label),
      .localKind = sourceLocalKind(label),
  });
  if (id.valid()) {
    annotationsById_.emplace(id.value, index);
  }
  return AnnotationBuilder{*this, id};
}

SourceAnnotation* SourceMapBuilder::annotation(SourceAnnotationId id) {
  const auto found = annotationsById_.find(id.value);
  if (found == annotationsById_.end() || found->second >= annotations_.size()) {
    return nullptr;
  }
  auto& annotation = annotations_[found->second];
  return annotation.id == id ? &annotation : nullptr;
}

std::string sourceLocalKind(std::string_view label) {
  std::string kind;
  bool pendingDash = false;
  for (const unsigned char ch : label) {
    if (std::isalnum(ch) != 0) {
      if (pendingDash && !kind.empty()) {
        kind.push_back('-');
      }
      kind.push_back(static_cast<char>(std::tolower(ch)));
      pendingDash = false;
    } else {
      pendingDash = true;
    }
  }
  return kind;
}

}  // namespace vgmtrans::core
