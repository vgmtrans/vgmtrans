/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/Export.h"

#include "core/DlsExporter.h"
#include "core/MidiExporter.h"
#include "core/SampleDecoder.h"
#include "core/SoundFontExporter.h"
#include "core/WavExporter.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iterator>
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

[[nodiscard]] const SequenceAsset* findSequenceAsset(const Project& project, AssetId id) {
  const auto found = std::ranges::find_if(project.assets, [id](const Asset& asset) {
    return metadata(asset).id == id && std::holds_alternative<SequenceAsset>(asset);
  });
  if (found == project.assets.end()) {
    return nullptr;
  }
  return std::get_if<SequenceAsset>(&*found);
}

[[nodiscard]] const SampleCollectionAsset* findSampleCollectionAsset(const Project& project, AssetId id) {
  const auto found = std::ranges::find_if(project.assets, [id](const Asset& asset) {
    return metadata(asset).id == id && std::holds_alternative<SampleCollectionAsset>(asset);
  });
  if (found == project.assets.end()) {
    return nullptr;
  }
  return std::get_if<SampleCollectionAsset>(&*found);
}

[[nodiscard]] const InstrumentBankAsset* findInstrumentBankAsset(const Project& project, AssetId id) {
  const auto found = std::ranges::find_if(project.assets, [id](const Asset& asset) {
    return metadata(asset).id == id && std::holds_alternative<InstrumentBankAsset>(asset);
  });
  if (found == project.assets.end()) {
    return nullptr;
  }
  return std::get_if<InstrumentBankAsset>(&*found);
}

[[nodiscard]] std::string artifactBaseName(const Collection& collection) {
  if (!collection.name.empty()) {
    return collection.name;
  }
  return "collection-" + std::to_string(collection.id.value);
}

[[nodiscard]] std::string filenamePart(std::string name) {
  if (name.empty()) {
    return "unnamed";
  }

  for (char& ch : name) {
    const auto value = static_cast<unsigned char>(ch);
    if (std::iscntrl(value) || ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
        ch == '<' || ch == '>' || ch == '|') {
      ch = '_';
    }
  }
  return name;
}

[[nodiscard]] std::string sampleArtifactName(const Collection& collection, const Sample& sample, u32 sampleIndex) {
  std::string sampleName = sample.name.empty() ? "sample-" + std::to_string(sampleIndex) : sample.name;
  return filenamePart(artifactBaseName(collection)) + "-" + std::to_string(sampleIndex) + "-" +
         filenamePart(std::move(sampleName)) + ".wav";
}

[[nodiscard]] std::vector<ExportKind> requestedKinds(const ExportRequest& request) {
  if (!request.kinds.empty()) {
    return request.kinds;
  }
  return {ExportKind::Midi};
}

[[nodiscard]] Artifact exportMidi(const Project& project, const Collection& collection, const ExportRequest& request,
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

[[nodiscard]] std::vector<Artifact> exportWav(const Project& project, const SourceStore& sources,
                                              const Collection& collection) {
  if (collection.sampleCollections.empty()) {
    return {Artifact{
        .filename = filenamePart(artifactBaseName(collection)) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {exportError("Collection does not reference a sample collection asset")},
    }};
  }

  std::vector<Artifact> artifacts;
  auto decoders = SampleDecoderRegistry::withDefaultDecoders();
  const WavExporter exporter;
  u32 sampleIndex = 0;

  for (const auto sampleCollectionId : collection.sampleCollections) {
    const auto* sampleCollection = findSampleCollectionAsset(project, sampleCollectionId);
    if (sampleCollection == nullptr) {
      artifacts.push_back(Artifact{
          .filename = filenamePart(artifactBaseName(collection)) + "-samples.wav",
          .mediaType = "audio/wav",
          .diagnostics = {exportError("Collection sample collection asset was not found")},
      });
      continue;
    }

    for (const auto& sample : sampleCollection->samples.samples) {
      Artifact artifact{
          .filename = sampleArtifactName(collection, sample, sampleIndex++),
          .mediaType = "audio/wav",
      };

      try {
        if (!sources.contains(sample.encodedData.source)) {
          artifact.diagnostics.push_back(exportError("Sample source was not found"));
        } else if (auto decoded = decoders.decode(sample, sources.bytes(sample.encodedData.source))) {
          artifact.bytes = exporter.exportPcm16(*decoded);
        } else {
          artifact.diagnostics.push_back(exportError("No decoder registered for sample codec"));
        }
      } catch (const std::exception& ex) {
        artifact.diagnostics.push_back(exportError(ex.what()));
      }

      artifacts.push_back(std::move(artifact));
    }
  }

  if (artifacts.empty()) {
    artifacts.push_back(Artifact{
        .filename = filenamePart(artifactBaseName(collection)) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {exportError("Collection sample collection assets did not contain samples")},
    });
  }

  return artifacts;
}

[[nodiscard]] Artifact exportSoundFont2(const Project& project, const SourceStore& sources,
                                        const Collection& collection) {
  std::vector<const InstrumentBankAsset*> instrumentBanks;
  instrumentBanks.reserve(collection.instrumentBanks.size());
  std::vector<const SampleCollectionAsset*> sampleCollections;
  sampleCollections.reserve(collection.sampleCollections.size());
  std::vector<Diagnostic> diagnostics;

  for (const auto id : collection.instrumentBanks) {
    if (const auto* instrumentBank = findInstrumentBankAsset(project, id)) {
      instrumentBanks.push_back(instrumentBank);
    } else {
      diagnostics.push_back(exportError("Collection instrument bank asset was not found"));
    }
  }

  for (const auto id : collection.sampleCollections) {
    if (const auto* sampleCollection = findSampleCollectionAsset(project, id)) {
      sampleCollections.push_back(sampleCollection);
    } else {
      diagnostics.push_back(exportError("Collection sample collection asset was not found"));
    }
  }

  auto result = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = artifactBaseName(collection),
          .instrumentBanks = instrumentBanks,
          .sampleCollections = sampleCollections,
      },
      sources);
  diagnostics.insert(diagnostics.end(), std::make_move_iterator(result.diagnostics.begin()),
                     std::make_move_iterator(result.diagnostics.end()));

  return Artifact{
      .filename = filenamePart(artifactBaseName(collection)) + ".sf2",
      .mediaType = "audio/soundfont",
      .bytes = std::move(result.bytes),
      .diagnostics = std::move(diagnostics),
  };
}

[[nodiscard]] Artifact exportDls(const Project& project, const SourceStore& sources, const Collection& collection) {
  std::vector<const InstrumentBankAsset*> instrumentBanks;
  instrumentBanks.reserve(collection.instrumentBanks.size());
  std::vector<const SampleCollectionAsset*> sampleCollections;
  sampleCollections.reserve(collection.sampleCollections.size());
  std::vector<Diagnostic> diagnostics;

  for (const auto id : collection.instrumentBanks) {
    if (const auto* instrumentBank = findInstrumentBankAsset(project, id)) {
      instrumentBanks.push_back(instrumentBank);
    } else {
      diagnostics.push_back(exportError("Collection instrument bank asset was not found"));
    }
  }

  for (const auto id : collection.sampleCollections) {
    if (const auto* sampleCollection = findSampleCollectionAsset(project, id)) {
      sampleCollections.push_back(sampleCollection);
    } else {
      diagnostics.push_back(exportError("Collection sample collection asset was not found"));
    }
  }

  auto result = DlsExporter().exportDls(
      DlsInput{
          .name = artifactBaseName(collection),
          .instrumentBanks = instrumentBanks,
          .sampleCollections = sampleCollections,
      },
      sources);
  diagnostics.insert(diagnostics.end(), std::make_move_iterator(result.diagnostics.begin()),
                     std::make_move_iterator(result.diagnostics.end()));

  return Artifact{
      .filename = filenamePart(artifactBaseName(collection)) + ".dls",
      .mediaType = "audio/dls",
      .bytes = std::move(result.bytes),
      .diagnostics = std::move(diagnostics),
  };
}

}  // namespace

std::vector<Artifact> ExportService::exportCollection(const Project& project, const SourceStore& sources,
                                                      CollectionId collection, const ExportRequest& request,
                                                      const SequencerProfileRegistry& profiles) const {
  const auto found = std::ranges::find_if(
      project.collections, [collection](const Collection& candidate) { return candidate.id == collection; });

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
      case ExportKind::Wav: {
        auto wavArtifacts = exportWav(project, sources, *found);
        artifacts.insert(artifacts.end(), std::make_move_iterator(wavArtifacts.begin()),
                         std::make_move_iterator(wavArtifacts.end()));
        break;
      }
      case ExportKind::SoundFont2:
        artifacts.push_back(exportSoundFont2(project, sources, *found));
        break;
      case ExportKind::Dls:
        artifacts.push_back(exportDls(project, sources, *found));
        break;
    }
  }

  return artifacts;
}

}  // namespace vgmtrans::core
