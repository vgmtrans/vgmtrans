/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/Export.h"

#include "value/export/DlsExporter.h"
#include "value/export/ExportDiagnostics.h"
#include "value/export/MidiExporter.h"
#include "value/core/MidiSequenceBuilder.h"
#include "value/core/ModulationAnalysis.h"
#include "value/core/ProjectModel.h"
#include "value/core/SampleDecoder.h"
#include "value/core/Source.h"
#include "value/export/ModulationScaling.h"
#include "value/export/SoundFontExporter.h"
#include "value/export/WavExporter.h"

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

[[nodiscard]] std::string sampleArtifactName(const std::string& baseName, const Sample& sample, u32 sampleIndex) {
  std::string sampleName = sample.name.empty() ? "sample-" + std::to_string(sampleIndex) : sample.name;
  return filenamePart(baseName) + "-" + std::to_string(sampleIndex) + "-" + filenamePart(std::move(sampleName)) +
         ".wav";
}

[[nodiscard]] std::vector<ExportKind> requestedKinds(const ExportRequest& request) {
  if (!request.kinds.empty()) {
    return request.kinds;
  }
  return {ExportKind::Midi};
}

struct PreparedCollectionExport {
  std::string baseName;
  CollectionAssets assets;
};

struct MidiLoweringResult {
  std::optional<MidiSequence> sequence;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] PreparedCollectionExport prepareCollectionExport(CollectionAssets assets) {
  const std::string baseName = assets.collection != nullptr ? artifactBaseName(*assets.collection) : "collection";
  return PreparedCollectionExport{
      .baseName = baseName,
      .assets = std::move(assets),
  };
}

[[nodiscard]] std::string midiSequenceProfileName(const SequenceAsset& sequence) {
  return sequence.commandSequence.midiSequenceProfile.empty() ? sequence.metadata.format
                                                              : sequence.commandSequence.midiSequenceProfile;
}

[[nodiscard]] MidiLoweringResult lowerMidiSequence(
    const PreparedCollectionExport& prepared,
    const MidiSequenceProfileRegistry& profiles,
    LoopPolicy loopPolicy) {
  if (!prepared.assets.diagnostics.collection.empty()) {
    return MidiLoweringResult{
        .diagnostics = prepared.assets.diagnostics.collection,
    };
  }
  if (prepared.assets.sequence == nullptr) {
    auto diagnostics = prepared.assets.diagnostics.sequence;
    if (diagnostics.empty()) {
      diagnostics.push_back(exportError("Collection does not reference a sequence asset"));
    }
    return MidiLoweringResult{
        .diagnostics = std::move(diagnostics),
    };
  }

  const std::string profileName = midiSequenceProfileName(*prepared.assets.sequence);
  // Some formats scan as one asset format but need a dialect-specific MIDI sequence profile.
  auto profile = profiles.create(profileName);
  if (!profile) {
    return MidiLoweringResult{
        .diagnostics = {exportError("No MIDI sequence profile registered for '" + profileName + "'")},
    };
  }

  return MidiLoweringResult{
      .sequence = MidiSequenceBuilder().build(prepared.assets.sequence->commandSequence, *profile, loopPolicy),
  };
}

[[nodiscard]] std::optional<MidiModulationUsage> midiModulationUsage(const MidiLoweringResult& lowering) {
  if (!lowering.sequence) {
    return std::nullopt;
  }

  auto usage = analyzeMidiModulationUsage(*lowering.sequence);
  if (!hasMidiModulationUsage(usage)) {
    return std::nullopt;
  }
  return usage;
}

[[nodiscard]] Artifact exportMidi(const PreparedCollectionExport& prepared, const ExportRequest& request,
                                  const MidiLoweringResult& lowering) {
  if (!lowering.sequence) {
    return Artifact{
        .filename = prepared.baseName + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = lowering.diagnostics,
    };
  }

  auto midiSequence = *lowering.sequence;
  if (request.synthModulationScaling == ModulationScalingPolicy::ObservedSequenceRange) {
    const auto usage = analyzeMidiModulationUsage(midiSequence);
    if (hasMidiModulationUsage(usage)) {
      applyMidiModulationScaling(midiSequence, usage, request.synthModulationScaling);
    }
  }
  auto bytes = MidiExporter().exportMidi(midiSequence);

  return Artifact{
      .filename = prepared.baseName + ".mid",
      .mediaType = "audio/midi",
      .bytes = std::move(bytes),
      .diagnostics = std::move(midiSequence.diagnostics),
  };
}

[[nodiscard]] std::vector<Artifact> exportWav(const PreparedCollectionExport& prepared, const SourceStore& sources) {
  if (prepared.assets.collection == nullptr || prepared.assets.collection->sampleCollections.empty()) {
    return {Artifact{
        .filename = filenamePart(prepared.baseName) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {exportError("Collection does not reference a sample collection asset")},
    }};
  }

  std::vector<Artifact> artifacts;
  auto decoders = SampleDecoderRegistry::withDefaultDecoders();
  const WavExporter exporter;
  u32 sampleIndex = 0;

  for (const auto& diagnostic : prepared.assets.diagnostics.sampleCollections) {
    artifacts.push_back(Artifact{
        .filename = filenamePart(prepared.baseName) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {diagnostic},
    });
  }

  for (const auto* sampleCollection : prepared.assets.sampleCollections) {
    for (const auto& sample : sampleCollection->samples.samples) {
      Artifact artifact{
          .filename = sampleArtifactName(prepared.baseName, sample, sampleIndex++),
          .mediaType = "audio/wav",
      };

      try {
        // Sample bytes stay in SourceStore so WAV export can report source-backed decode errors.
        if (!sources.contains(sample.encodedData.source)) {
          artifact.diagnostics.push_back(
              exportError("Sample source was not found", validDiagnosticRange(sample.encodedData)));
        } else if (auto decoded = decoders.decode(sample, sources.bytes(sample.encodedData.source))) {
          artifact.bytes = exporter.exportPcm16(*decoded);
        } else {
          artifact.diagnostics.push_back(
              exportError("No decoder registered for sample codec", validDiagnosticRange(sample.encodedData)));
        }
      } catch (const std::exception& ex) {
        artifact.diagnostics.push_back(exportError(ex.what(), validDiagnosticRange(sample.encodedData)));
      }

      artifacts.push_back(std::move(artifact));
    }
  }

  if (artifacts.empty()) {
    artifacts.push_back(Artifact{
        .filename = filenamePart(prepared.baseName) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {exportError("Collection sample collection assets did not contain samples")},
    });
  }

  return artifacts;
}

[[nodiscard]] Artifact exportSoundFont2(const PreparedCollectionExport& prepared, const SourceStore& sources,
                                        const ExportRequest& request, const MidiModulationUsage* midiModulation) {
  auto result = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = prepared.baseName,
          .instrumentSets = prepared.assets.instrumentSets,
          .sampleCollections = prepared.assets.sampleCollections,
          .midiModulationUsage = midiModulation,
          .modulationScaling = request.synthModulationScaling,
      },
      sources);
  auto diagnostics = prepared.assets.diagnostics.instrumentSets;
  diagnostics.insert(diagnostics.end(), prepared.assets.diagnostics.sampleCollections.begin(),
                     prepared.assets.diagnostics.sampleCollections.end());
  diagnostics.insert(diagnostics.end(), std::make_move_iterator(result.diagnostics.begin()),
                     std::make_move_iterator(result.diagnostics.end()));

  return Artifact{
      .filename = filenamePart(prepared.baseName) + ".sf2",
      .mediaType = "audio/soundfont",
      .bytes = std::move(result.bytes),
      .diagnostics = std::move(diagnostics),
  };
}

[[nodiscard]] Artifact exportDls(const PreparedCollectionExport& prepared, const SourceStore& sources,
                                 const ExportRequest& request, const MidiModulationUsage* midiModulation) {
  auto result = DlsExporter().exportDls(
      DlsInput{
          .name = prepared.baseName,
          .instrumentSets = prepared.assets.instrumentSets,
          .sampleCollections = prepared.assets.sampleCollections,
          .midiModulationUsage = midiModulation,
          .modulationScaling = request.synthModulationScaling,
      },
      sources);
  auto diagnostics = prepared.assets.diagnostics.instrumentSets;
  diagnostics.insert(diagnostics.end(), prepared.assets.diagnostics.sampleCollections.begin(),
                     prepared.assets.diagnostics.sampleCollections.end());
  diagnostics.insert(diagnostics.end(), std::make_move_iterator(result.diagnostics.begin()),
                     std::make_move_iterator(result.diagnostics.end()));

  return Artifact{
      .filename = filenamePart(prepared.baseName) + ".dls",
      .mediaType = "audio/dls",
      .bytes = std::move(result.bytes),
      .diagnostics = std::move(diagnostics),
  };
}

}  // namespace

std::vector<Artifact> ExportService::exportCollection(const Project& project, const SourceStore& sources,
                                                      CollectionId collection, const ExportRequest& request,
                                                      const MidiSequenceProfileRegistry& profiles) const {
  auto resolved = resolveCollectionAssets(project, collection);
  if (resolved.collection == nullptr) {
    auto diagnostics = resolved.diagnostics.collection;
    if (diagnostics.empty()) {
      diagnostics.push_back(exportError("CollectionId was not found in the Project snapshot"));
    }
    return {Artifact{
        .filename = "export-error.txt",
        .mediaType = "text/plain",
        .diagnostics = std::move(diagnostics),
    }};
  }

  std::vector<Artifact> artifacts;
  const auto prepared = prepareCollectionExport(std::move(resolved));
  std::optional<MidiLoweringResult> midiLowering;
  std::optional<MidiModulationUsage> midiUsage;
  bool midiUsageAnalyzed = false;

  const auto requireMidiLowering = [&]() -> const MidiLoweringResult& {
    if (!midiLowering) {
      midiLowering = lowerMidiSequence(prepared, profiles, request.loopPolicy);
    }
    return *midiLowering;
  };

  const auto requireMidiModulationUsage = [&]() -> const MidiModulationUsage* {
    if (request.synthModulationScaling != ModulationScalingPolicy::ObservedSequenceRange) {
      return nullptr;
    }
    if (!midiUsageAnalyzed) {
      midiUsage = midiModulationUsage(requireMidiLowering());
      midiUsageAnalyzed = true;
    }
    return midiUsage ? &*midiUsage : nullptr;
  };

  for (const auto kind : requestedKinds(request)) {
    switch (kind) {
      case ExportKind::Midi:
        artifacts.push_back(exportMidi(prepared, request, requireMidiLowering()));
        break;
      case ExportKind::Wav: {
        auto wavArtifacts = exportWav(prepared, sources);
        artifacts.insert(artifacts.end(), std::make_move_iterator(wavArtifacts.begin()),
                         std::make_move_iterator(wavArtifacts.end()));
        break;
      }
      case ExportKind::SoundFont2:
        artifacts.push_back(exportSoundFont2(prepared, sources, request, requireMidiModulationUsage()));
        break;
      case ExportKind::Dls:
        artifacts.push_back(exportDls(prepared, sources, request, requireMidiModulationUsage()));
        break;
    }
  }

  return artifacts;
}

std::vector<CollectionExport> ExportService::exportAllCollections(const Project& project, const SourceStore& sources,
                                                                  const ExportRequest& request,
                                                                  const MidiSequenceProfileRegistry& profiles) const {
  std::vector<CollectionExport> exports;
  exports.reserve(project.collections.size());
  for (const auto& collection : project.collections) {
    exports.push_back(CollectionExport{
        .collection = collection.id,
        .artifacts = exportCollection(project, sources, collection.id, request, profiles),
    });
  }
  return exports;
}

}  // namespace vgmtrans::core
