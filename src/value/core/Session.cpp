/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/Session.h"

#include "value/core/Scan.h"
#include "value/export/Export.h"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

namespace {

void addMissingSequenceDialectDiagnostics(Project& project, const SequenceDialectRegistry& dialects) {
  for (const auto& asset : project.assets) {
    const auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
    if (sequence == nullptr || dialects.contains(sequence->program.dialect.value)) {
      continue;
    }

    project.diagnostics.push_back(Diagnostic{
        .severity = Severity::Error,
        .message = "No sequence dialect registered for '" + sequence->program.dialect.value + "'",
        .range = sequence->metadata.range.valid() ? std::optional<SourceRange>{sequence->metadata.range}
                                                  : std::nullopt,
    });
  }
}

}  // namespace

SourceId Session::addSource(SourceFile file, std::vector<u8> bytes) {
  // User-added sources invalidate virtual extractions from the previous scan.
  sources_.discardVirtualizedTail();
  project_ = {};
  return sources_.add(std::move(file), std::move(bytes));
}

SourceId Session::addSourceFromPath(std::filesystem::path path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("failed to open source file: " + path.string());
  }

  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if (size < 0) {
    throw std::runtime_error("failed to stat source file: " + path.string());
  }
  file.seekg(0, std::ios::beg);

  std::vector<u8> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!file) {
    throw std::runtime_error("failed to read source file: " + path.string());
  }

  return addSource(
      SourceFile{
          .name = path.filename().string(),
          .path = std::move(path),
      },
      std::move(bytes));
}

Project Session::scan() {
  project_ = scanProject(sources_, formats_);
  addMissingSequenceDialectDiagnostics(project_, dialects_);
  return project_;
}

std::vector<Artifact> Session::exportCollection(CollectionId id, const ExportRequest& request) const {
  return core::exportCollection(project_, sources_, id, request, dialects_);
}

std::vector<CollectionExport> Session::exportAllCollections(const ExportRequest& request) const {
  return core::exportAllCollections(project_, sources_, request, dialects_);
}

}  // namespace vgmtrans::core
