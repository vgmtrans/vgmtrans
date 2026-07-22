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
#include "value/model/SessionSnapshot.h"
#include "value/scan/FormatRegistry.h"
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

[[nodiscard]] std::string artifactBaseName(const SequenceProgramAsset& sequence) {
  if (!sequence.metadata.name.empty()) {
    return filenamePart(sequence.metadata.name);
  }
  return "sequence-" + std::to_string(sequence.metadata.id.value);
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
  std::vector<InstrumentSetAsset> preparedInstrumentSets;
};

struct MidiLoweringResult {
  std::optional<PerformanceSequence> performance;
  std::optional<MidiSequence> sequence;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] PreparedCollectionExport prepareCollectionExport(CollectionAssets assets, const SessionSnapshot& snapshot,
                                                               const SourceStore& sources,
                                                               const FormatRegistry* formats) {
  // Prepare once per collection so all requested artifacts share names, resolved assets,
  // and reference diagnostics.
  const std::string baseName = assets.collection != nullptr ? artifactBaseName(*assets.collection) : "collection";
  PreparedCollectionExport prepared{
      .baseName = baseName,
      .assets = std::move(assets),
  };
  if (formats == nullptr || prepared.assets.collection == nullptr) {
    return prepared;
  }

  for (const auto& module : formats->modules()) {
    const std::string_view resolver =
        module.collectionResolverId.empty() ? std::string_view(module.name) : module.collectionResolverId;
    if (module.prepareCollection == nullptr || resolver != prepared.assets.collection->key.resolver) {
      continue;
    }

    try {
      auto result = module.prepareCollection(CollectionPrepareContext{
          .sources = sources,
          .snapshot = snapshot,
          .collection = *prepared.assets.collection,
      });
      prepared.assets.diagnostics.instrumentSets.insert(prepared.assets.diagnostics.instrumentSets.end(),
                                                        std::make_move_iterator(result.diagnostics.begin()),
                                                        std::make_move_iterator(result.diagnostics.end()));
      prepared.preparedInstrumentSets = std::move(result.instrumentSets);
      prepared.assets.instrumentSets.reserve(prepared.assets.instrumentSets.size() +
                                             prepared.preparedInstrumentSets.size());
      for (const auto& instrumentSet : prepared.preparedInstrumentSets) {
        prepared.assets.instrumentSets.push_back(&instrumentSet);
      }
    } catch (const std::exception& ex) {
      prepared.assets.diagnostics.instrumentSets.push_back(
          exportError(module.name + " collection preparation failed: " + ex.what()));
    }
    break;
  }
  return prepared;
}

[[nodiscard]] MidiLoweringResult lowerMidiSequence(const SequenceProgramAsset& sequence,
                                                   std::span<const InstrumentSetAsset* const> instrumentSets,
                                                   const SequenceDialectRegistry& dialects, LoopPolicy loopPolicy,
                                                   u32 sequenceLoops, const MidiExportOptions& midiOptions,
                                                   ModulationConversionPolicy modulationConversion) {
  const auto* dialect = dialects.find(sequence.program.dialect.value);
  if (dialect == nullptr) {
    return MidiLoweringResult{
        .diagnostics = {exportError("No sequence dialect registered for '" + sequence.program.dialect.value + "'",
                                    validDiagnosticRange(sequence.metadata.range))},
    };
  }

  auto performance = SequenceVm(SequenceVmOptions{
                                    .loopPolicy = loopPolicy,
                                    .sequenceLoops = sequenceLoops,
                                })
                         .render(sequence.program, *dialect);
  auto midi = PerformanceMidiRenderer().render(performance, midiOptions, modulationConversion, instrumentSets);
  return MidiLoweringResult{
      .performance = std::move(performance),
      .sequence = std::move(midi),
  };
}

[[nodiscard]] MidiLoweringResult lowerCollectionMidiSequence(const PreparedCollectionExport& prepared,
                                                             const SequenceDialectRegistry& dialects,
                                                             const ExportRequest& request) {
  // Rendering the source sequence is needed for .mid output and for observed-range
  // modulation. Keep failures as diagnostics so other exports can still run.
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

  return lowerMidiSequence(*prepared.assets.sequenceProgram, prepared.assets.instrumentSets, dialects,
                           request.loopPolicy, request.sequenceLoops, request.midi, request.modulationConversion);
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

[[nodiscard]] Artifact exportMidi(std::string_view baseName, const MidiLoweringResult& lowering,
                                  ModulationScalingPolicy modulationScaling,
                                  ModulationConversionPolicy modulationConversion) {
  if (!lowering.sequence) {
    return Artifact{
        .filename = std::string(baseName) + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = lowering.diagnostics,
    };
  }

  auto midiSequence = *lowering.sequence;
  if (modulationConversion == ModulationConversionPolicy::SynthModulators &&
      modulationScaling == ModulationScalingPolicy::ObservedSequenceRange) {
    // Apply the same observed-range scaling to MIDI controller values and synth
    // modulators so they continue to match each other.
    const auto usage = lowering.performance ? analyzePerformanceModulationUsage(*lowering.performance)
                                            : analyzeMidiModulationUsage(midiSequence);
    if (hasMidiModulationUsage(usage)) {
      applyMidiModulationScaling(midiSequence, usage, modulationScaling);
    }
  }
  auto bytes = MidiExporter().exportMidi(midiSequence);

  return Artifact{
      .filename = std::string(baseName) + ".mid",
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
                                        const ExportRequest& request, const MidiModulationUsage* midiModulation,
                                        ModulationConversionPolicy modulationConversion) {
  auto result = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = prepared.baseName,
          .instrumentSets = prepared.assets.instrumentSets,
          .sampleCollections = prepared.assets.sampleCollections,
          .midiModulationUsage = midiModulation,
          .modulationScaling = request.modulationScaling,
          .modulationConversion = modulationConversion,
      },
      sources);
  // Asset-resolution diagnostics describe missing references; exporter diagnostics
  // describe failures encountered while writing the container.
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
                                 const ExportRequest& request, const MidiModulationUsage* midiModulation,
                                 ModulationConversionPolicy modulationConversion) {
  auto result = DlsExporter().exportDls(
      DlsInput{
          .name = prepared.baseName,
          .instrumentSets = prepared.assets.instrumentSets,
          .sampleCollections = prepared.assets.sampleCollections,
          .midiModulationUsage = midiModulation,
          .modulationScaling = request.modulationScaling,
          .modulationConversion = modulationConversion,
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

Artifact exportSequenceMidi(const SessionSnapshot& snapshot, AssetId sequenceId, const SequenceExportRequest& request,
                            const SequenceDialectRegistry& dialects) {
  const auto* asset = snapshot.asset(sequenceId);
  const auto* sequence = asset != nullptr ? std::get_if<SequenceProgramAsset>(asset) : nullptr;
  if (sequence == nullptr) {
    std::vector<Diagnostic> diagnostics;
    if (asset == nullptr) {
      diagnostics.push_back(exportError("Sequence asset was not found"));
    } else {
      diagnostics.push_back(exportError("Asset is not a sequence", validDiagnosticRange(metadata(*asset).range)));
    }
    return Artifact{
        .filename = "sequence-" + std::to_string(sequenceId.value) + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = std::move(diagnostics),
    };
  }

  const auto lowering = lowerMidiSequence(*sequence, {}, dialects, request.loopPolicy, request.sequenceLoops,
                                          request.midi, ModulationConversionPolicy::SequenceEventSimulation);
  return exportMidi(artifactBaseName(*sequence), lowering, ModulationScalingPolicy::FullFormatRange,
                    ModulationConversionPolicy::SequenceEventSimulation);
}

CollectionPlayback prepareCollectionPlayback(const SessionSnapshot& snapshot, const SourceStore& sources,
                                             CollectionId collection, const PlaybackRequest& request,
                                             const SequenceDialectRegistry& dialects, const FormatRegistry* formats) {
  CollectionPlayback playback{
      .collection = collection,
  };
  auto resolved = resolveCollectionAssets(snapshot, collection);
  if (resolved.collection == nullptr) {
    playback.diagnostics = std::move(resolved.diagnostics.collection);
    if (playback.diagnostics.empty()) {
      playback.diagnostics.push_back(exportError("CollectionId was not found in the SessionSnapshot"));
    }
    return playback;
  }

  const auto prepared = prepareCollectionExport(std::move(resolved), snapshot, sources, formats);
  playback.title = prepared.baseName;
  if (prepared.assets.sequenceProgram != nullptr) {
    playback.sequence = prepared.assets.sequenceProgram->metadata.id;
    playback.assetDependencies.push_back(playback.sequence);
  }
  playback.assetDependencies.insert(playback.assetDependencies.end(),
                                    prepared.assets.collection->instrumentSets.begin(),
                                    prepared.assets.collection->instrumentSets.end());
  playback.assetDependencies.insert(playback.assetDependencies.end(),
                                    prepared.assets.collection->sampleCollections.begin(),
                                    prepared.assets.collection->sampleCollections.end());

  const ExportRequest exportRequest{
      .loopPolicy = request.loopPolicy,
      .sequenceLoops = request.sequenceLoops,
      .midi = request.midi,
      .modulationScaling = ModulationScalingPolicy::FullFormatRange,
      .modulationConversion = ModulationConversionPolicy::SequenceEventSimulation,
  };
  auto lowering = lowerCollectionMidiSequence(prepared, dialects, exportRequest);
  auto midi =
      exportMidi(prepared.baseName, lowering, exportRequest.modulationScaling, exportRequest.modulationConversion);
  auto soundFont = exportSoundFont2(prepared, sources, exportRequest, nullptr,
                                    lowering.sequence ? ModulationConversionPolicy::SequenceEventSimulation
                                                      : ModulationConversionPolicy::SynthModulators);

  playback.midi = std::move(midi.bytes);
  playback.soundFont = std::move(soundFont.bytes);
  playback.diagnostics = std::move(midi.diagnostics);
  playback.diagnostics.insert(playback.diagnostics.end(), std::make_move_iterator(soundFont.diagnostics.begin()),
                              std::make_move_iterator(soundFont.diagnostics.end()));
  if (lowering.performance) {
    playback.performance = std::move(*lowering.performance);
  }
  return playback;
}

std::vector<Artifact> exportCollection(const SessionSnapshot& snapshot, const SourceStore& sources,
                                       CollectionId collection, const ExportRequest& request,
                                       const SequenceDialectRegistry& dialects, const FormatRegistry* formats) {
  auto resolved = resolveCollectionAssets(snapshot, collection);
  if (resolved.collection == nullptr) {
    auto diagnostics = resolved.diagnostics.collection;
    if (diagnostics.empty()) {
      diagnostics.push_back(exportError("CollectionId was not found in the SessionSnapshot"));
    }
    return {Artifact{
        .filename = "export-error.txt",
        .mediaType = "text/plain",
        .diagnostics = std::move(diagnostics),
    }};
  }

  std::vector<Artifact> artifacts;
  const auto prepared = prepareCollectionExport(std::move(resolved), snapshot, sources, formats);
  std::optional<MidiLoweringResult> midiLowering;
  std::optional<MidiModulationUsage> midiUsage;
  bool midiUsageAnalyzed = false;
  const auto kinds = requestedKinds(request);
  const bool exportsMidi = std::ranges::find(kinds, ExportKind::Midi) != kinds.end();

  const auto requireMidiLowering = [&]() -> const MidiLoweringResult& {
    // Several requested files can depend on the same rendered sequence. Render it
    // once so diagnostics and modulation analysis refer to the same playback data.
    if (!midiLowering) {
      midiLowering = lowerCollectionMidiSequence(prepared, dialects, request);
    }
    return *midiLowering;
  };

  const auto requireMidiModulationUsage = [&]() -> const MidiModulationUsage* {
    // Synth exporters only need observed MIDI modulation when the policy asks for it.
    // WAV and plain MIDI export should not pay that analysis cost.
    if (request.modulationScaling != ModulationScalingPolicy::ObservedSequenceRange) {
      return nullptr;
    }
    if (!midiUsageAnalyzed) {
      midiUsage = midiModulationUsage(requireMidiLowering());
      midiUsageAnalyzed = true;
    }
    return midiUsage ? &*midiUsage : nullptr;
  };

  const auto synthModulationConversion = [&]() {
    if (request.modulationConversion == ModulationConversionPolicy::SynthModulators) {
      return ModulationConversionPolicy::SynthModulators;
    }
    // Sequence-event simulation is a replacement, not a reason to discard an
    // instrument's modulation. Keep native modulation unless a MIDI artifact
    // was requested and successfully written alongside the synth file.
    if (!exportsMidi || !requireMidiLowering().sequence) {
      return ModulationConversionPolicy::SynthModulators;
    }
    return ModulationConversionPolicy::SequenceEventSimulation;
  };

  for (const auto kind : kinds) {
    switch (kind) {
      case ExportKind::Midi:
        artifacts.push_back(exportMidi(prepared.baseName, requireMidiLowering(), request.modulationScaling,
                                       request.modulationConversion));
        break;
      case ExportKind::Wav: {
        auto wavArtifacts = exportWav(prepared, sources);
        artifacts.insert(artifacts.end(), std::make_move_iterator(wavArtifacts.begin()),
                         std::make_move_iterator(wavArtifacts.end()));
        break;
      }
      case ExportKind::SoundFont2: {
        const auto conversion = synthModulationConversion();
        artifacts.push_back(exportSoundFont2(
            prepared, sources, request,
            conversion == ModulationConversionPolicy::SynthModulators ? requireMidiModulationUsage() : nullptr,
            conversion));
        break;
      }
      case ExportKind::Dls: {
        const auto conversion = synthModulationConversion();
        artifacts.push_back(exportDls(
            prepared, sources, request,
            conversion == ModulationConversionPolicy::SynthModulators ? requireMidiModulationUsage() : nullptr,
            conversion));
        break;
      }
    }
  }

  return artifacts;
}

std::vector<CollectionExport> exportAllCollections(const SessionSnapshot& snapshot, const SourceStore& sources,
                                                   const ExportRequest& request,
                                                   const SequenceDialectRegistry& dialects,
                                                   const FormatRegistry* formats) {
  std::vector<CollectionExport> exports;
  exports.reserve(snapshot.collections().size());
  for (const auto& collection : snapshot.collections()) {
    exports.push_back(CollectionExport{
        .collection = collection.id,
        .artifacts = exportCollection(snapshot, sources, collection.id, request, dialects, formats),
    });
  }
  return exports;
}

}  // namespace vgmtrans::core
