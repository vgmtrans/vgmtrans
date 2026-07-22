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
#include "value/scan/FormatDefinition.h"
#include "value/scan/ScanTypes.h"
#include "value/sequence/SequenceDialect.h"
#include "value/session/AssetStore.h"
#include "value/session/CollectionStore.h"
#include "value/session/DiagnosticStore.h"
#include "value/session/ExplicitCollectionStore.h"
#include "value/session/MatchFactStore.h"
#include "value/session/SourceMapStore.h"

#include <filesystem>
#include <memory>
#include <set>
#include <span>
#include <unordered_set>
#include <vector>

namespace vgmtrans::core {

// Session is the mutable state for one loaded workspace. It owns the source
// bytes, the assets found inside them, the facts used to match related assets,
// the collections built from those matches, and the format registries.
// Call snapshot() when UI, tests, or export need a stable read-only view.
class Session {
public:
  void registerFormat(FormatDefinition definition);
  void registerFormat(FormatModule module);
  void registerFormat(FormatModule module, SequenceDialect dialect);

  SourceId addSource(SourceFile file, std::vector<u8> bytes);
  SourceId addSourceFromPath(std::filesystem::path path);
  [[nodiscard]] SessionSnapshot removeSource(SourceId id);
  [[nodiscard]] SessionSnapshot removeAssets(std::span<const AssetId> assets);

  [[nodiscard]] SessionSnapshot scanSource(SourceId id);
  [[nodiscard]] SessionSnapshot scanPendingSources();
  [[nodiscard]] SessionSnapshot snapshot() const;
  [[nodiscard]] std::shared_ptr<const SourceInspection> inspect(AssetId asset) const;

  [[nodiscard]] CollectionPlayback preparePlayback(CollectionId id, const PlaybackRequest& request) const;
  [[nodiscard]] Artifact exportSequenceMidi(AssetId id, const SequenceExportRequest& request) const;
  [[nodiscard]] Artifact exportInstrumentSet(AssetId id, ExportKind kind, const ExportRequest& request) const;
  [[nodiscard]] std::vector<Artifact> exportCollection(CollectionId id, const ExportRequest& request) const;
  [[nodiscard]] std::vector<CollectionExport> exportAllCollections(const ExportRequest& request) const;

  [[nodiscard]] const SourceStore& sources() const noexcept { return sources_; }
  [[nodiscard]] const FormatRegistry& formats() const noexcept { return formats_; }
  [[nodiscard]] const SequenceDialectRegistry& dialects() const noexcept { return dialects_; }

private:
  void sealRegistries() noexcept;
  void scanSourceAndDerived(SourceId id);
  void scanOneSource(SourceId id, std::vector<SourceId>& queue, std::set<u32>& queued);
  void addExtractedSources(std::vector<ExtractedSource> extractedSources, SourceId defaultParent,
                           std::vector<SourceId>& queue, std::set<u32>& queued);
  void removeDiscoveredDataForSources(const std::vector<SourceId>& sources);
  void rebuildCollections();

  SourceStore sources_;
  // These stores keep cleanup and validation rules beside the data they protect.
  // Session coordinates source loading, scanning, and export around them.
  AssetStore assets_;
  MatchFactStore matchFacts_;
  ExplicitCollectionStore explicitCollections_;
  SourceMapStore sourceMaps_;
  CollectionStore collections_;
  DiagnosticStore diagnostics_;
  FormatRegistry formats_;
  SequenceDialectRegistry dialects_;
  ScanIdAllocator ids_;
  std::unordered_set<u32> scannedSources_;
};

}  // namespace vgmtrans::core
