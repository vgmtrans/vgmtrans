/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/MetadataModel.h"
#include "value/model/SourceMap.h"

#include <optional>
#include <span>
#include <vector>

namespace vgmtrans::core {

class Session;

// Identifies either a persistent source annotation or one of its transient
// field projections inside an immutable inspection. Field indexes are stable
// for the lifetime of that inspection and never escape into the session model
// as fabricated SourceAnnotationIds.
struct SourceInspectionItem {
  SourceAnnotationId annotation;
  std::optional<u32> field;

  [[nodiscard]] static SourceInspectionItem forAnnotation(SourceAnnotationId annotation) {
    return SourceInspectionItem{.annotation = annotation};
  }
  [[nodiscard]] static SourceInspectionItem forField(SourceAnnotationId annotation, u32 field) {
    return SourceInspectionItem{.annotation = annotation, .field = field};
  }
  [[nodiscard]] bool valid() const noexcept { return annotation.valid(); }
  [[nodiscard]] bool isField() const noexcept { return field.has_value(); }

  friend bool operator==(const SourceInspectionItem&, const SourceInspectionItem&) = default;
};

// Immutable source data for one asset. The annotation graph is copied from the
// session, while the underlying source blob is shared and never
// mutated. An open inspection therefore remains valid after the session changes.
class SourceInspection final {
public:
  [[nodiscard]] AssetId asset() const noexcept { return metadata_.id; }
  [[nodiscard]] const AssetMetadata& metadata() const noexcept { return metadata_; }
  [[nodiscard]] SourceRange range() const noexcept { return range_; }
  [[nodiscard]] std::span<const u8> bytes() const noexcept;
  [[nodiscard]] const SharedSequence<SourceAnnotation>& annotations() const noexcept {
    return sourceMap_.annotations();
  }

  [[nodiscard]] const SourceAnnotation* annotation(SourceAnnotationId id) const;
  [[nodiscard]] const SourceAnnotation* annotation(SourceInspectionItem item) const;
  [[nodiscard]] const SourceField* field(SourceInspectionItem item) const;
  [[nodiscard]] SourceRange range(SourceInspectionItem item) const;
  [[nodiscard]] std::span<const SourceAnnotationId> roots() const noexcept { return roots_; }
  [[nodiscard]] std::vector<SourceAnnotationId> children(SourceAnnotationId parent) const;
  [[nodiscard]] std::optional<SourceAnnotationId> annotationAt(u64 offset) const;
  [[nodiscard]] std::optional<SourceInspectionItem> itemAt(u64 offset) const;

private:
  friend class Session;

  static std::shared_ptr<const SourceInspection> create(AssetMetadata metadata, SourceMap sourceMap,
                                                        SharedSourceBytes sourceBytes);
  SourceInspection(AssetMetadata metadata, SourceRange range, SharedSourceBytes sourceBytes, SourceMap sourceMap);

  void sortBySourceOrder(std::vector<SourceAnnotationId>& annotations) const;

  AssetMetadata metadata_;
  SourceRange range_;
  SharedSourceBytes sourceBytes_;
  SourceMap sourceMap_;
  std::vector<SourceAnnotationId> roots_;
};

}  // namespace vgmtrans::core
