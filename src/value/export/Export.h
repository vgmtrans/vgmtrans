/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/sequence/PerformanceModel.h"

namespace vgmtrans::core {

class FormatRegistry;
class SessionSnapshot;
class SourceStore;

// Standard MIDI and SoundFont data prepared from one rendered performance.
// The retained performance supplies the source timeline used by inspectors.
struct CollectionPlayback {
  CollectionId collection;
  AssetId sequence;
  std::vector<AssetId> assetDependencies;
  std::string title;
  PerformanceSequence performance;
  std::vector<u8> midi;
  std::vector<u8> soundFont;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool playable() const noexcept {
    return collection.valid() && sequence.valid() && !midi.empty() && !soundFont.empty();
  }
};

[[nodiscard]] Artifact exportSequenceMidi(const SessionSnapshot& snapshot, const SourceStore& sources, AssetId sequence,
                                          const SequenceExportRequest& request, const FormatRegistry& formats);

[[nodiscard]] Artifact exportInstrumentSet(const SessionSnapshot& snapshot, const SourceStore& sources,
                                           AssetId instrumentSet, SynthExportFormat format,
                                           const ExportRequest& request, const FormatRegistry& formats);

[[nodiscard]] CollectionPlayback prepareCollectionPlayback(const SessionSnapshot& snapshot, const SourceStore& sources,
                                                           CollectionId collection, const PlaybackRequest& request,
                                                           const FormatRegistry& formats);

[[nodiscard]] std::vector<Artifact> exportCollection(const SessionSnapshot& snapshot, const SourceStore& sources,
                                                     CollectionId collection, const ExportRequest& request,
                                                     const FormatRegistry& formats);

[[nodiscard]] std::vector<CollectionExport> exportAllCollections(const SessionSnapshot& snapshot,
                                                                 const SourceStore& sources,
                                                                 const ExportRequest& request,
                                                                 const FormatRegistry& formats);

}  // namespace vgmtrans::core
