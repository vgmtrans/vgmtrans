/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/Export.h"

#include "core/MidiExporter.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] Diagnostic exportError(std::string message) {
  return Diagnostic{
      .severity = Severity::Error,
      .message = std::move(message),
  };
}

[[nodiscard]] Diagnostic exportWarning(std::string message) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
  };
}

[[nodiscard]] const SequenceAsset* findSequenceAsset(const Project& project, AssetId id) {
  const auto found = std::ranges::find_if(project.assets, [id](const Asset& asset) {
    return metadata(asset).id == id && std::holds_alternative<SequenceAsset>(asset);
  });
  if (found == project.assets.end()) {
    return nullptr;
  }
  return std::get_if<SequenceAsset>(&*found);
}

[[nodiscard]] std::string artifactBaseName(const Collection& collection) {
  if (!collection.name.empty()) {
    return collection.name;
  }
  return "collection-" + std::to_string(collection.id.value);
}

[[nodiscard]] std::vector<ExportKind> requestedKinds(const ExportRequest& request) {
  if (!request.kinds.empty()) {
    return request.kinds;
  }
  return {ExportKind::Midi};
}

[[nodiscard]] Artifact exportMidi(
    const Project& project,
    const Collection& collection,
    const ExportRequest& request,
    const SequencerProfileRegistry& profiles) {
  if (!collection.sequence) {
    return Artifact{
        .filename = artifactBaseName(collection) + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = {exportError("Collection does not reference a sequence asset")},
    };
  }

  const auto* sequence = findSequenceAsset(project, *collection.sequence);
  if (sequence == nullptr) {
    return Artifact{
        .filename = artifactBaseName(collection) + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = {exportError("Collection sequence asset was not found")},
    };
  }

  auto profile = profiles.create(sequence->metadata.format);
  if (!profile) {
    return Artifact{
        .filename = artifactBaseName(collection) + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = {exportError("No sequencer profile registered for format '" + sequence->metadata.format + "'")},
    };
  }

  auto lowered = PerformanceLowerer().lower(sequence->program, *profile, request.loopPolicy);
  auto bytes = MidiExporter().exportMidi(lowered);

  return Artifact{
      .filename = artifactBaseName(collection) + ".mid",
      .mediaType = "audio/midi",
      .bytes = std::move(bytes),
      .diagnostics = std::move(lowered.diagnostics),
  };
}

}  // namespace

std::vector<Artifact> ExportService::exportCollection(
    const Project& project,
    const SourceStore&,
    CollectionId collection,
    const ExportRequest& request,
    const SequencerProfileRegistry& profiles) const {
  const auto found = std::ranges::find_if(project.collections, [collection](const Collection& candidate) {
    return candidate.id == collection;
  });

  if (found == project.collections.end()) {
    return {Artifact{
        .filename = "export-error.txt",
        .mediaType = "text/plain",
        .diagnostics = {Diagnostic{
            .severity = Severity::Error,
            .message = "CollectionId was not found in the Project snapshot",
        }},
    }};
  }

  std::vector<Artifact> artifacts;
  for (const auto kind : requestedKinds(request)) {
    switch (kind) {
      case ExportKind::Midi:
        artifacts.push_back(exportMidi(project, *found, request, profiles));
        break;
      case ExportKind::SoundFont2:
      case ExportKind::Dls:
      case ExportKind::Wav:
        artifacts.push_back(Artifact{
            .filename = artifactBaseName(*found) + "-export-unimplemented.txt",
            .mediaType = "text/plain",
            .diagnostics = {exportWarning("Requested export kind is not implemented in the value pipeline yet")},
        });
        break;
    }
  }

  return artifacts;
}

}  // namespace vgmtrans::core
