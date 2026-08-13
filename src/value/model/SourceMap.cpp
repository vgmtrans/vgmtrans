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

[[nodiscard]] std::vector<SourceAnnotationId> idsFromAnnotations(const SharedSequence<SourceAnnotation>& annotations,
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
  return ObjectRef{.kind = ObjectKind::InstrumentIndex, .index0 = instrumentIndex};
}

ObjectRef ObjectRefs::instrumentProgram(u32 bank, u32 program) {
  return ObjectRef{.kind = ObjectKind::InstrumentProgram, .index0 = bank, .index1 = program};
}

ObjectRef ObjectRefs::sample(AssetId sampleSetAsset, u32 sampleIndex) {
  return ObjectRef{.kind = ObjectKind::Sample, .asset = sampleSetAsset, .index0 = sampleIndex};
}

ObjectRef ObjectRefs::sampleIndex(u32 sampleIndex) {
  return ObjectRef{.kind = ObjectKind::SampleIndex, .index0 = sampleIndex};
}

ObjectRef ObjectRefs::misc(AssetId miscAsset) {
  return ObjectRef{.kind = ObjectKind::Misc, .asset = miscAsset};
}

SourceMap::Index::Index(const std::vector<SourceAnnotation>& annotations) {
  annotationsById.reserve(annotations.size());
  for (size_t i = 0; i < annotations.size(); ++i) {
    const auto id = annotations[i].id;
    if (id.valid()) {
      const auto [_, inserted] = annotationsById.emplace(id.value, i);
      if (!inserted) {
        throw std::logic_error("Duplicate SourceAnnotationId in SourceMap");
      }
    }
    if (annotations[i].range.source.valid()) {
      annotationsBySource[annotations[i].range.source.value].push_back(id);
    }
    if (annotations[i].parent && annotations[i].parent->valid()) {
      annotationsByParent[annotations[i].parent->value].push_back(id);
    }
  }

  // Ownership is inherited through parent annotations. Resolve each chain once
  // while building this immutable index; malformed cycles are reported by scan
  // validation before the map can enter session state.
  assetOwnerByAnnotation.resize(annotations.size());
  std::vector<u8> state(annotations.size());
  std::vector<size_t> path;
  for (size_t root = 0; root < annotations.size(); ++root) {
    if (!annotations[root].id.valid() || state[root] == 2) {
      continue;
    }

    path.clear();
    std::optional<AssetId> owner;
    size_t current = root;
    while (true) {
      if (state[current] == 2) {
        owner = assetOwnerByAnnotation[current];
        break;
      }
      if (state[current] == 1) {
        break;
      }

      state[current] = 1;
      path.push_back(current);
      const auto& annotation = annotations[current];
      if (annotation.owner && annotation.owner->asset.valid()) {
        owner = annotation.owner->asset;
        break;
      }
      if (!annotation.parent || !annotation.parent->valid()) {
        break;
      }
      const auto parent = annotationsById.find(annotation.parent->value);
      if (parent == annotationsById.end()) {
        break;
      }
      current = parent->second;
    }

    for (const size_t index : path) {
      assetOwnerByAnnotation[index] = owner;
      state[index] = 2;
    }
  }

  for (size_t i = 0; i < annotations.size(); ++i) {
    if (annotations[i].id.valid() && assetOwnerByAnnotation[i]) {
      annotationsByAsset[assetOwnerByAnnotation[i]->value].push_back(annotations[i].id);
    }
  }
}

SourceMap::Storage::Storage(std::vector<Part> partsValue) : parts(std::move(partsValue)) {
  std::vector<std::shared_ptr<const std::vector<SourceAnnotation>>> chunks;
  chunks.reserve(parts.size());
  for (const auto& part : parts) {
    chunks.push_back(part.annotations);
  }
  annotations = detail::SharedSequenceAccess::fromChunks(std::move(chunks));
}

SourceMap::SourceMap() : storage_(emptyStorage()) {
}

SourceMap::SourceMap(std::vector<SourceAnnotation> annotations) {
  if (annotations.empty()) {
    storage_ = emptyStorage();
    return;
  }
  auto values = std::make_shared<const std::vector<SourceAnnotation>>(std::move(annotations));
  auto index = std::make_shared<const Index>(*values);
  storage_ = std::make_shared<const Storage>(std::vector<Part>{{std::move(values), std::move(index)}});
}

SourceMap::SourceMap(std::shared_ptr<const Storage> storage) : storage_(std::move(storage)) {
}

bool SourceMap::empty() const noexcept {
  return storage_->annotations.empty();
}

const SharedSequence<SourceAnnotation>& SourceMap::annotations() const noexcept {
  return storage_->annotations;
}

const SourceAnnotation* SourceMap::find(SourceAnnotationId id) const {
  for (const auto& part : storage_->parts) {
    const auto found = part.index->annotationsById.find(id.value);
    if (found != part.index->annotationsById.end()) {
      return &(*part.annotations)[found->second];
    }
  }
  return nullptr;
}

const SourceAnnotation& SourceMap::get(SourceAnnotationId id) const {
  const auto* annotation = find(id);
  if (annotation == nullptr) {
    throw std::out_of_range("SourceAnnotationId was not found in SourceMap");
  }
  return *annotation;
}

std::vector<SourceAnnotationId> SourceMap::annotationsForSource(SourceId source) const {
  std::vector<SourceAnnotationId> annotations;
  for (const auto& part : storage_->parts) {
    const auto found = part.index->annotationsBySource.find(source.value);
    if (found != part.index->annotationsBySource.end()) {
      annotations.insert(annotations.end(), found->second.begin(), found->second.end());
    }
  }
  return annotations;
}

std::vector<SourceAnnotationId> SourceMap::intersecting(SourceRange range) const {
  return idsFromAnnotations(
      annotations(), [&](const SourceAnnotation& annotation) { return rangesIntersect(annotation.range, range); });
}

std::vector<SourceAnnotationId> SourceMap::containing(SourceRange range) const {
  return idsFromAnnotations(annotations(),
                            [&](const SourceAnnotation& annotation) { return rangeContains(annotation.range, range); });
}

std::vector<SourceAnnotationId> SourceMap::at(SourceId source, u64 offset) const {
  return idsFromAnnotations(annotations(), [&](const SourceAnnotation& annotation) {
    return rangeContainsOffset(annotation.range, source, offset);
  });
}

std::vector<SourceAnnotationId> SourceMap::ownedBy(ObjectRef object) const {
  return idsFromAnnotations(annotations(), [&](const SourceAnnotation& annotation) {
    return annotation.owner && *annotation.owner == object;
  });
}

std::optional<AssetId> SourceMap::assetOwner(SourceAnnotationId id) const {
  if (!id.valid()) {
    return std::nullopt;
  }
  for (const auto& part : storage_->parts) {
    const auto found = part.index->annotationsById.find(id.value);
    if (found != part.index->annotationsById.end()) {
      return part.index->assetOwnerByAnnotation[found->second];
    }
  }
  return std::nullopt;
}

std::vector<SourceAnnotationId> SourceMap::annotationsForAsset(AssetId asset) const {
  std::vector<SourceAnnotationId> annotations;
  if (!asset.valid()) {
    return annotations;
  }
  for (const auto& part : storage_->parts) {
    const auto found = part.index->annotationsByAsset.find(asset.value);
    if (found != part.index->annotationsByAsset.end()) {
      annotations.insert(annotations.end(), found->second.begin(), found->second.end());
    }
  }
  return annotations;
}

std::vector<SourceAnnotationId> SourceMap::childrenOf(SourceAnnotationId parent) const {
  std::vector<SourceAnnotationId> children;
  for (const auto& part : storage_->parts) {
    const auto found = part.index->annotationsByParent.find(parent.value);
    if (found != part.index->annotationsByParent.end()) {
      children.insert(children.end(), found->second.begin(), found->second.end());
    }
  }
  return children;
}

std::vector<SourceAnnotationId> SourceMap::withRole(SourceId source, SourceRole role) const {
  return idsFromAnnotations(annotations(), [&](const SourceAnnotation& annotation) {
    return annotation.range.source == source && annotation.role == role;
  });
}

std::vector<SourceAnnotationId> SourceMap::withSequenceSemantic(SourceId source, SequenceSemantic semantic) const {
  return idsFromAnnotations(annotations(), [&](const SourceAnnotation& annotation) {
    return annotation.range.source == source && annotation.sequenceSemantic == semantic;
  });
}

std::vector<SourceLink> SourceMap::linksFrom(SourceAnnotationId id) const {
  const auto* annotation = find(id);
  return annotation != nullptr ? annotation->links : std::vector<SourceLink>{};
}

std::vector<SourceAnnotationId> SourceMap::linksTo(const SourceTarget& target) const {
  return idsFromAnnotations(annotations(), [&](const SourceAnnotation& annotation) {
    return std::ranges::any_of(annotation.links, [&](const SourceLink& link) { return link.target == target; });
  });
}

SourceMap SourceMap::join(std::span<const SourceMap> maps) {
  size_t partCount = 0;
  for (const auto& map : maps) {
    partCount += map.storage_->parts.size();
  }

  std::vector<Part> parts;
  parts.reserve(partCount);
  for (const auto& map : maps) {
    parts.insert(parts.end(), map.storage_->parts.begin(), map.storage_->parts.end());
  }
  if (parts.empty()) {
    return {};
  }
  return SourceMap{std::make_shared<const Storage>(std::move(parts))};
}

std::shared_ptr<const SourceMap::Storage> SourceMap::emptyStorage() {
  static const auto empty = std::make_shared<const Storage>(std::vector<Part>{});
  return empty;
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

AnnotationBuilder& AnnotationBuilder::fieldsAsChildren(bool enabled) {
  if (auto* found = annotation()) {
    found->fieldsAsChildren = enabled;
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

// Turns a parsed record into one source annotation so its overall byte range
// and decoded fields stay together in the source map.
AnnotationBuilder SourceMapBuilder::annotation(SourceRole role, std::string_view label, const SourceRecord& record) {
  return add(role, label, record.range).fields(record.fields);
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
