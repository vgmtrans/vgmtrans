/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"

#include <span>
#include <vector>

namespace vgmtrans::core {

class FormatRegistry;
class SessionSnapshot;
class SourceStore;

struct CollectionStitchBank {
  u32 source = 0;
  u32 target = 0;

  friend bool operator==(const CollectionStitchBank&, const CollectionStitchBank&) noexcept = default;
};

struct CollectionStitchPart {
  CollectionId collection;
  u64 startTick = 0;
  // Only source banks used by this part are assigned; numeric gaps are not reserved.
  std::vector<CollectionStitchBank> banks;
};

struct CollectionStitchResult {
  Artifact midi;
  Artifact soundFont;
  std::vector<CollectionStitchPart> parts;

  [[nodiscard]] bool complete() const noexcept { return !midi.bytes.empty() && !soundFont.bytes.empty(); }
};

// Stitching always produces MIDI plus SF2; request supplies its preparation
// and filtering policies, while request.kinds is intentionally ignored.
[[nodiscard]] CollectionStitchResult stitchCollections(const SessionSnapshot& snapshot, const SourceStore& sources,
                                                       std::span<const CollectionId> collections,
                                                       const ExportRequest& request, const FormatRegistry& formats);

}  // namespace vgmtrans::core
