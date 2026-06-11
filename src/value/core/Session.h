/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/Export.h"
#include "value/core/FormatModule.h"
#include "value/core/MidiSequenceProfile.h"
#include "value/core/Source.h"

#include <filesystem>
#include <vector>

namespace vgmtrans::core {

class Session {
 public:
  SourceId addSource(SourceFile file, std::vector<u8> bytes);
  SourceId addSourceFromPath(std::filesystem::path path);

  [[nodiscard]] Project scan();

  [[nodiscard]] std::vector<Artifact> exportCollection(
      CollectionId id,
      const ExportRequest& request) const;

  [[nodiscard]] std::vector<CollectionExport> exportAllCollections(
      const ExportRequest& request) const;

  [[nodiscard]] const SourceStore& sources() const noexcept { return sources_; }
  [[nodiscard]] SourceStore& sources() noexcept { return sources_; }
  [[nodiscard]] const FormatRegistry& formats() const noexcept { return formats_; }
  [[nodiscard]] FormatRegistry& formats() noexcept { return formats_; }
  [[nodiscard]] const MidiSequenceProfileRegistry& profiles() const noexcept { return profiles_; }
  [[nodiscard]] MidiSequenceProfileRegistry& profiles() noexcept { return profiles_; }
  [[nodiscard]] const Project& project() const noexcept { return project_; }

 private:
  SourceStore sources_;
  FormatRegistry formats_;
  MidiSequenceProfileRegistry profiles_;
  Project project_;
  ScanService scanner_;
  ExportService exporter_;
};

}  // namespace vgmtrans::core
