/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/export/Export.h"
#include "value/model/SessionSnapshot.h"
#include "value/model/SourceInspection.h"
#include "value/scan/FormatRegistry.h"
#include "value/scan/ScanTypes.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

class SessionState;
struct CollectionStitchResult;

// Session is the mutable state for one loaded workspace. It owns the source
// bytes, the assets found inside them, their collections, and the format
// registry.
// Call snapshot() when UI, tests, or export need a stable read-only view.
class Session {
public:
  Session();
  ~Session();
  Session(Session&&) noexcept;
  Session& operator=(Session&&) noexcept;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  void registerFormat(FormatModule module);
  void registerExtractor(SourceExtractor extractor);

  SourceId addSource(SourceFile file, std::vector<u8> bytes);
  SourceId addSourceFromPath(std::filesystem::path path);
  void removeSource(SourceId id);
  void removeSources(std::span<const SourceId> ids);
  void removeAssets(std::span<const AssetId> assets);
  [[nodiscard]] CollectionId createUserCollection(std::string name, CollectionMembers members);

  void scanSource(SourceId id);
  void scanPendingSources();
  [[nodiscard]] SessionSnapshot snapshot() const;
  [[nodiscard]] std::shared_ptr<const SourceInspection> inspect(AssetId asset) const;

  [[nodiscard]] CollectionPlayback preparePlayback(CollectionId id, const PlaybackRequest& request) const;
  [[nodiscard]] Artifact exportSequenceMidi(AssetId id, const SequenceExportRequest& request) const;
  [[nodiscard]] Artifact exportSoundBank(AssetId id, SynthExportFormat format, const ExportRequest& request) const;
  [[nodiscard]] std::vector<Artifact> exportSamples(AssetId id) const;
  [[nodiscard]] std::vector<Artifact> exportCollection(CollectionId id, const ExportRequest& request) const;
  [[nodiscard]] std::vector<CollectionExport> exportAllCollections(const ExportRequest& request) const;
  [[nodiscard]] CollectionStitchResult stitchCollections(std::span<const CollectionId> collections,
                                                         const ExportRequest& request) const;

  [[nodiscard]] const SourceStore& sources() const noexcept { return sources_; }
  [[nodiscard]] const FormatRegistry& formats() const noexcept { return formats_; }

private:
  void invalidateSnapshot() noexcept;
  void sealFormats() noexcept;
  void scanSourceAndDerived(SourceId id);
  void scanOneSource(SourceId source, std::vector<SourceId>& queue, std::set<u32>& queued);
  void addExtractedSources(std::vector<ExtractedSource> extractedSources, SourceId defaultParent,
                           std::vector<SourceId>& queue, std::set<u32>& queued);
  void removeSourceFamily(SourceId source, std::vector<SourceId>& removed);
  void rebuildCollections();

  SourceStore sources_;
  std::unique_ptr<SessionState> state_;
  FormatRegistry formats_;
  ScanIdAllocator ids_;
  std::unordered_set<u32> scannedSources_;
  // Session is single-thread-confined. This memoized immutable revision is
  // populated on the first snapshot read after a mutation.
  mutable std::optional<SessionSnapshot> snapshotCache_;
};

}  // namespace vgmtrans::core
