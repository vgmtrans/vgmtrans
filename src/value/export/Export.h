/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"

namespace vgmtrans::core {

class MidiSequenceProfileRegistry;
class SourceStore;
struct Project;

class ExportService {
 public:
  [[nodiscard]] std::vector<Artifact> exportCollection(
      const Project& project,
      const SourceStore& sources,
      CollectionId collection,
      const ExportRequest& request,
      const MidiSequenceProfileRegistry& profiles) const;

  [[nodiscard]] std::vector<CollectionExport> exportAllCollections(
      const Project& project,
      const SourceStore& sources,
      const ExportRequest& request,
      const MidiSequenceProfileRegistry& profiles) const;
};

}  // namespace vgmtrans::core
