/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/core/Model.h"
#include "value/core/MidiSequenceBuilder.h"
#include "value/core/Source.h"

#include <string>
#include <vector>

namespace vgmtrans::core {

enum class ExportKind {
  Midi,
  SoundFont2,
  Dls,
  Wav,
};

struct ExportRequest {
  std::vector<ExportKind> kinds;
  LoopPolicy loopPolicy = LoopPolicy::Default;
};

struct Artifact {
  std::string filename;
  std::string mediaType;
  std::vector<u8> bytes;
  std::vector<Diagnostic> diagnostics;
};

struct CollectionExport {
  CollectionId collection;
  std::vector<Artifact> artifacts;
};

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
