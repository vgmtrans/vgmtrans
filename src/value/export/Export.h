/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"
#include "value/sequence/PerformanceModel.h"

namespace vgmtrans::core {

class SequenceDialectRegistry;
class FormatRegistry;
class SessionSnapshot;
class SourceStore;

struct PlaybackRequest {
  LoopPolicy loopPolicy = LoopPolicy::Default;
  u32 sequenceLoops = 1;
  MidiExportOptions midi;
};

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

[[nodiscard]] Artifact exportSequenceMidi(const SessionSnapshot& snapshot, AssetId sequence,
                                          const SequenceExportRequest& request,
                                          const SequenceDialectRegistry& dialects);
[[nodiscard]] Artifact exportSequenceMidi(const SessionSnapshot& snapshot, const SourceStore& sources,
                                          AssetId sequence, const SequenceExportRequest& request,
                                          const SequenceDialectRegistry& dialects,
                                          const FormatRegistry* formats = nullptr);

[[nodiscard]] Artifact exportInstrumentSet(const SessionSnapshot& snapshot, const SourceStore& sources,
                                           AssetId instrumentSet, ExportKind kind,
                                           const ExportRequest& request,
                                           const SequenceDialectRegistry& dialects,
                                           const FormatRegistry* formats = nullptr);

[[nodiscard]] CollectionPlayback prepareCollectionPlayback(const SessionSnapshot& snapshot, const SourceStore& sources,
                                                           CollectionId collection, const PlaybackRequest& request,
                                                           const SequenceDialectRegistry& dialects,
                                                           const FormatRegistry* formats = nullptr);

[[nodiscard]] std::vector<Artifact> exportCollection(const SessionSnapshot& snapshot, const SourceStore& sources,
                                                     CollectionId collection, const ExportRequest& request,
                                                     const SequenceDialectRegistry& dialects,
                                                     const FormatRegistry* formats = nullptr);

[[nodiscard]] std::vector<CollectionExport> exportAllCollections(const SessionSnapshot& snapshot,
                                                                 const SourceStore& sources,
                                                                 const ExportRequest& request,
                                                                 const SequenceDialectRegistry& dialects,
                                                                 const FormatRegistry* formats = nullptr);

}  // namespace vgmtrans::core
