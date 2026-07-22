/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "application/WorkspaceController.h"

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace vgmtrans::ui {

// Immutable value projection shared by the source tree and HexView. It owns the
// bytes and annotations needed by an inspector tab, so neither view retains
// pointers into a SessionSnapshot or the mutable Session source store.
class SourceInspectorModel final {
public:
  SourceInspectorModel(const WorkspaceController& workspace, core::AssetId asset);

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] core::AssetId asset() const noexcept { return asset_; }
  [[nodiscard]] const core::AssetMetadata& metadata() const noexcept { return metadata_; }
  [[nodiscard]] core::SourceRange range() const noexcept { return range_; }
  [[nodiscard]] std::span<const u8> bytes() const noexcept { return bytes_; }
  [[nodiscard]] std::span<const core::SourceAnnotation> annotations() const noexcept { return annotations_; }

  [[nodiscard]] const core::SourceAnnotation* annotation(core::SourceAnnotationId id) const;
  [[nodiscard]] std::span<const core::SourceAnnotationId> roots() const noexcept { return roots_; }
  [[nodiscard]] std::span<const core::SourceAnnotationId> children(core::SourceAnnotationId parent) const;
  [[nodiscard]] std::optional<core::SourceAnnotationId> annotationAt(u64 offset) const;

private:
  void buildHierarchy();

  bool valid_ = false;
  core::AssetId asset_;
  core::AssetMetadata metadata_;
  core::SourceRange range_;
  std::vector<u8> bytes_;
  std::vector<core::SourceAnnotation> annotations_;
  std::unordered_map<u32, size_t> annotationIndexes_;
  std::vector<core::SourceAnnotationId> roots_;
  std::unordered_map<u32, std::vector<core::SourceAnnotationId>> children_;
};

}  // namespace vgmtrans::ui
