/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/Export.h"

#include "value/export/synth/DlsExporter.h"
#include "value/export/ExportDiagnostics.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/ModulationAnalysis.h"
#include "value/model/ProjectModel.h"
#include "value/synth/SampleDecoder.h"
#include "value/sequence/SequenceVm.h"
#include "value/base/Source.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/SoundFontExporter.h"
#include "value/export/audio/WavExporter.h"

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
  // MIDI remains the default single artifact because it only requires a sequence asset.
  // Synth/sample exports must be requested explicitly.
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
  std::optional<PerformanceSequence> performance;
  std::optional<MidiSequence> sequence;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] PreparedCollectionExport prepareCollectionExport(CollectionAssets assets) {
  // Prepare once per collection so all requested artifacts share names, resolved assets,
  // and reference diagnostics.
  const std::string baseName = assets.collection != nullptr ? artifactBaseName(*assets.collection) : "collection";
  return PreparedCollectionExport{
      .baseName = baseName,
      .assets = std::move(assets),
  };
}

[[nodiscard]] MidiLoweringResult lowerMidiSequence(const PreparedCollectionExport& prepared,
                                                   const SequenceDialectRegistry& dialects,
                                                   const ExportRequest& request) {
  // Sequence rendering is needed both for .mid output and for observed modulation scaling
  // in synth exports. Keep failures as diagnostics so non-MIDI artifacts can still be built.
  if (!prepared.assets.diagnostics.collection.empty()) {
    return MidiLoweringResult{
        .diagnostics = prepared.assets.diagnostics.collection,
    };
  }
  if (prepared.assets.sequenceProgram == nullptr) {
    auto diagnostics = prepared.assets.diagnostics.sequence;
    if (diagnostics.empty()) {
      diagnostics.push_back(exportError("Collection does not reference a sequence asset"));
    }
    return MidiLoweringResult{
        .diagnostics = std::move(diagnostics),
    };
  }

  const auto& sequence = *prepared.assets.sequenceProgram;
  const auto* dialect = dialects.find(sequence.program.dialect.value);
  if (dialect == nullptr) {
    return MidiLoweringResult{
        .diagnostics = {exportError("No sequence dialect registered for '" + sequence.program.dialect.value + "'",
                                    validDiagnosticRange(sequence.metadata.range))},
    };
  }

  auto performance = SequenceVm(request.loopPolicy).render(sequence.program, *dialect);
  auto midi = PerformanceMidiRenderer().render(performance, request.midi);
  return MidiLoweringResult{
      .performance = std::move(performance),
      .sequence = std::move(midi),
  };
}

[[nodiscard]] std::optional<MidiModulationUsage> midiModulationUsage(const MidiLoweringResult& lowering) {
  // SF2/DLS modulators often have only 7-bit controller inputs. Observed ranges let us
  // trade theoretical format coverage for better practical resolution when requested.
  if (!lowering.performance) {
    return std::nullopt;
  }

  auto usage = analyzePerformanceModulationUsage(*lowering.performance);
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
    // MIDI export also applies the same scaling so controller values and synth modulators
    // agree when a user asks for observed-range modulation.
    const auto usage = lowering.performance ? analyzePerformanceModulationUsage(*lowering.performance)
                                            : analyzeMidiModulationUsage(midiSequence);
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
  // Asset-resolution diagnostics describe missing references; exporter diagnostics
  // describe failures encountered while materializing the container.
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
  // Keep DLS diagnostic merging parallel to SF2 so callers can compare both exports
  // without learning two error-reporting conventions.
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

std::vector<Artifact> exportCollection(const Project& project, const SourceStore& sources, CollectionId collection,
                                       const ExportRequest& request, const SequenceDialectRegistry& dialects) {
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
    // Several requested artifacts can depend on the same lowered MIDI sequence. Lower it
    // once so diagnostics and modulation analysis all refer to identical playback data.
    if (!midiLowering) {
      midiLowering = lowerMidiSequence(prepared, dialects, request);
    }
    return *midiLowering;
  };

  const auto requireMidiModulationUsage = [&]() -> const MidiModulationUsage* {
    // Synth exporters only need observed MIDI modulation when the policy asks for it.
    // WAV and plain MIDI export should not pay that analysis cost.
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

std::vector<CollectionExport> exportAllCollections(const Project& project, const SourceStore& sources,
                                                   const ExportRequest& request,
                                                   const SequenceDialectRegistry& dialects) {
  std::vector<CollectionExport> exports;
  exports.reserve(project.collections.size());
  for (const auto& collection : project.collections) {
    exports.push_back(CollectionExport{
        .collection = collection.id,
        .artifacts = exportCollection(project, sources, collection.id, request, dialects),
    });
  }
  return exports;
}

}  // namespace vgmtrans::core
