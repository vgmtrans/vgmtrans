/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/Export.h"

#include "value/export/CollectionResolution.h"
#include "value/export/DynamicEnvelope.h"
#include "value/export/ExportDiagnostics.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/ModulationAnalysis.h"
#include "value/export/synth/SynthExportData.h"
#include "value/model/SessionSnapshot.h"
#include "value/scan/FormatRegistry.h"
#include "value/synth/SampleDecoder.h"
#include "value/base/Source.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/audio/WavExporter.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace vgmtrans::core {

namespace {

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

[[nodiscard]] std::string artifactBaseName(const InstrumentSetAsset& instrumentSet) {
  if (!instrumentSet.metadata.name.empty()) {
    return filenamePart(instrumentSet.metadata.name);
  }
  return "instrument-set-" + std::to_string(instrumentSet.metadata.id.value);
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

[[nodiscard]] std::optional<MidiSequence> renderMidi(const RenderedCollection& rendering,
                                                     std::span<const InstrumentSetAsset* const> instrumentSets,
                                                     const MidiExportOptions& options,
                                                     ModulationConversionPolicy modulationConversion,
                                                     const PerformanceSequence* preparedPerformance = nullptr) {
  const auto* performance = preparedPerformance;
  if (performance == nullptr && rendering.performance) {
    performance = &*rendering.performance;
  }
  if (performance == nullptr) {
    return std::nullopt;
  }
  return renderMidiSequence(*performance, options, modulationConversion, instrumentSets, &rendering.modulation);
}

[[nodiscard]] std::optional<MidiModulationUsage> midiModulationUsage(const RenderedCollection& rendering) {
  // SF2/DLS modulators often have only 7-bit controller inputs. Observed ranges let us
  // trade theoretical format coverage for better practical resolution when requested.
  if (!rendering.performance) {
    return std::nullopt;
  }

  auto usage = analyzePerformanceModulationUsage(*rendering.performance, &rendering.modulation);
  if (!hasMidiModulationUsage(usage)) {
    return std::nullopt;
  }
  return usage;
}

[[nodiscard]] std::vector<const InstrumentSetAsset*> instrumentView(
    std::span<const InstrumentSetAsset> instrumentSets) {
  std::vector<const InstrumentSetAsset*> view;
  view.reserve(instrumentSets.size());
  for (const auto& instruments : instrumentSets) {
    view.push_back(&instruments);
  }
  return view;
}

struct SynthCollectionView {
  std::string_view name;
  std::span<const InstrumentSetAsset* const> instrumentSets;
  std::span<const SampleCollectionAsset* const> sampleCollections;
  const CollectionResolutionDiagnostics& diagnostics;
};

[[nodiscard]] Artifact exportMidi(std::string_view baseName, const RenderedCollection& rendering,
                                  const std::optional<MidiSequence>& loweredMidi,
                                  ModulationScalingPolicy modulationScaling,
                                  ModulationConversionPolicy modulationConversion) {
  if (!loweredMidi) {
    return Artifact{
        .filename = std::string(baseName) + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = rendering.diagnostics,
    };
  }

  auto midiSequence = *loweredMidi;
  if (modulationConversion == ModulationConversionPolicy::SynthModulators &&
      modulationScaling == ModulationScalingPolicy::ObservedSequenceRange && rendering.performance) {
    // Apply the same observed-range scaling to MIDI controller values and synth
    // modulators so they continue to match each other.
    const auto usage = analyzePerformanceModulationUsage(*rendering.performance, &rendering.modulation);
    if (hasMidiModulationUsage(usage)) {
      applyMidiModulationScaling(midiSequence, usage, modulationScaling);
    }
  }
  auto bytes = encodeMidiFile(midiSequence);

  return Artifact{
      .filename = std::string(baseName) + ".mid",
      .mediaType = "audio/midi",
      .bytes = std::move(bytes),
      .diagnostics = std::move(midiSequence.diagnostics),
  };
}

[[nodiscard]] std::vector<Artifact> exportWav(const ResolvedCollection& resolved, const SourceStore& sources) {
  if (resolved.sampleCollections().empty() && resolved.diagnostics().sampleCollections.empty()) {
    return {Artifact{
        .filename = filenamePart(resolved.baseName()) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {exportError("Collection does not reference a sample collection asset")},
    }};
  }

  std::vector<Artifact> artifacts;
  u32 sampleIndex = 0;

  for (const auto& diagnostic : resolved.diagnostics().sampleCollections) {
    artifacts.push_back(Artifact{
        .filename = filenamePart(resolved.baseName()) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {diagnostic},
    });
  }

  for (const auto* sampleCollection : resolved.sampleCollections()) {
    for (const auto& sample : sampleCollection->samples.samples) {
      Artifact artifact{
          .filename = sampleArtifactName(resolved.baseName(), sample, sampleIndex++),
          .mediaType = "audio/wav",
      };

      try {
        // Sample bytes stay in SourceStore so WAV export can report source-backed decode errors.
        if (!sources.contains(sample.encodedData.source)) {
          artifact.diagnostics.push_back(
              exportError("Sample source was not found", validDiagnosticRange(sample.encodedData)));
        } else if (auto decoded = decodeSample(sample, sources.bytes(sample.encodedData.source))) {
          artifact.bytes = encodePcm16Wav(*decoded);
        } else {
          artifact.diagnostics.push_back(
              exportError("Unsupported sample codec", validDiagnosticRange(sample.encodedData)));
        }
      } catch (const std::exception& ex) {
        artifact.diagnostics.push_back(exportError(ex.what(), validDiagnosticRange(sample.encodedData)));
      }

      artifacts.push_back(std::move(artifact));
    }
  }

  if (artifacts.empty()) {
    artifacts.push_back(Artifact{
        .filename = filenamePart(resolved.baseName()) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {exportError("Collection sample collection assets did not contain samples")},
    });
  }

  return artifacts;
}

[[nodiscard]] SynthExportInput synthExportInput(const SynthCollectionView& collection, const ExportRequest& request,
                                                const FormatRegistry& formats,
                                                const MidiModulationUsage* midiModulation,
                                                ModulationConversionPolicy modulationConversion,
                                                const PerformanceSequence* sequenceUsage) {
  return SynthExportInput{
      .name = std::string(collection.name),
      .instrumentSets = collection.instrumentSets,
      .sampleCollections = collection.sampleCollections,
      .formats = &formats,
      .sequenceUsage = sequenceUsage,
      .midiModulationUsage = midiModulation,
      .modulationScaling = request.modulationScaling,
      .modulationConversion = modulationConversion,
      .sampleFiltering = request.sampleFiltering,
  };
}

[[nodiscard]] Artifact synthArtifact(const SynthCollectionView& collection, SynthExportResult result,
                                     std::string_view extension, std::string_view mediaType) {
  auto combinedDiagnostics = collection.diagnostics.instrumentSets;
  combinedDiagnostics.insert(combinedDiagnostics.end(), collection.diagnostics.sampleCollections.begin(),
                             collection.diagnostics.sampleCollections.end());
  combinedDiagnostics.insert(combinedDiagnostics.end(), std::make_move_iterator(result.diagnostics.begin()),
                             std::make_move_iterator(result.diagnostics.end()));

  return Artifact{
      .filename = filenamePart(std::string(collection.name)) + std::string(extension),
      .mediaType = std::string(mediaType),
      .bytes = std::move(result.bytes),
      .diagnostics = std::move(combinedDiagnostics),
  };
}

[[nodiscard]] Artifact exportSoundFont2(const SynthCollectionView& collection, const SourceStore& sources,
                                        const FormatRegistry& formats, const ExportRequest& request,
                                        const MidiModulationUsage* midiModulation,
                                        ModulationConversionPolicy modulationConversion,
                                        const PerformanceSequence* sequenceUsage = nullptr) {
  const auto input =
      synthExportInput(collection, request, formats, midiModulation, modulationConversion, sequenceUsage);
  return synthArtifact(collection, buildSoundFont2(input, sources), ".sf2", "audio/soundfont");
}

[[nodiscard]] Artifact exportDls(const SynthCollectionView& collection, const SourceStore& sources,
                                 const FormatRegistry& formats, const ExportRequest& request,
                                 const MidiModulationUsage* midiModulation,
                                 ModulationConversionPolicy modulationConversion,
                                 const PerformanceSequence* sequenceUsage = nullptr) {
  const auto input =
      synthExportInput(collection, request, formats, midiModulation, modulationConversion, sequenceUsage);
  return synthArtifact(collection, buildDls(input, sources), ".dls", "audio/dls");
}

Artifact exportStandaloneSequenceMidi(const SessionSnapshot& snapshot, AssetId sequenceId,
                                      const SequenceExportRequest& request, const FormatRegistry& formats) {
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

  const auto rendering = renderSequence(*sequence, request);
  const auto midi = renderMidi(rendering, {}, request.midi, ModulationConversionPolicy::SequenceEventSimulation);
  return exportMidi(artifactBaseName(*sequence), rendering, midi, ModulationScalingPolicy::FullFormatRange,
                    ModulationConversionPolicy::SequenceEventSimulation);
}

}  // namespace

Artifact exportSequenceMidi(const SessionSnapshot& snapshot, const SourceStore& sources, AssetId sequenceId,
                            const SequenceExportRequest& request, const FormatRegistry& formats) {
  const auto* sequence = snapshot.asset<SequenceProgramAsset>(sequenceId);
  const auto* collection = snapshot.firstCollectionContaining(sequenceId);
  if (sequence == nullptr || collection == nullptr) {
    return exportStandaloneSequenceMidi(snapshot, sequenceId, request, formats);
  }

  auto artifacts = exportCollection(snapshot, sources, collection->id,
                                    ExportRequest{
                                        .kinds = {ExportKind::Midi},
                                        .sequence = request,
                                        .modulationScaling = ModulationScalingPolicy::FullFormatRange,
                                        .modulationConversion = ModulationConversionPolicy::SequenceEventSimulation,
                                    },
                                    formats);
  if (artifacts.empty()) {
    return Artifact{
        .filename = artifactBaseName(*sequence) + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = {exportError("Collection MIDI export produced no artifact")},
    };
  }
  artifacts.front().filename = artifactBaseName(*sequence) + ".mid";
  return std::move(artifacts.front());
}

Artifact exportInstrumentSet(const SessionSnapshot& snapshot, const SourceStore& sources, AssetId instrumentSetId,
                             SynthExportFormat format, const ExportRequest& request, const FormatRegistry& formats) {
  const bool soundFont = format == SynthExportFormat::SoundFont2;
  const ExportKind kind = soundFont ? ExportKind::SoundFont2 : ExportKind::Dls;
  const std::string extension = soundFont ? ".sf2" : ".dls";
  const std::string mediaType = soundFont ? "audio/soundfont" : "audio/dls";
  const auto* asset = snapshot.asset(instrumentSetId);
  const auto* instrumentSet = asset != nullptr ? std::get_if<InstrumentSetAsset>(asset) : nullptr;
  if (instrumentSet == nullptr) {
    return Artifact{
        .filename = "instrument-set-" + std::to_string(instrumentSetId.value) + extension,
        .mediaType = mediaType,
        .diagnostics = {exportError(asset == nullptr ? "Instrument set asset was not found"
                                                     : "Asset is not an instrument set")},
    };
  }

  const std::string baseName = artifactBaseName(*instrumentSet);
  if (const auto* collection = snapshot.firstCollectionContaining(instrumentSetId)) {
    auto collectionRequest = request;
    collectionRequest.kinds = {kind};
    if (snapshot.countCollectionsContaining(instrumentSetId) > 1) {
      collectionRequest.exportOnlyUsedInstruments = false;
    }
    auto artifacts = exportCollection(snapshot, sources, collection->id, collectionRequest, formats);
    if (!artifacts.empty()) {
      artifacts.front().filename = baseName + extension;
      return std::move(artifacts.front());
    }
    return Artifact{
        .filename = baseName + extension,
        .mediaType = mediaType,
        .diagnostics = {exportError("Collection instrument export produced no artifact")},
    };
  }

  std::vector<InstrumentSetAsset> instrumentSets{*instrumentSet};
  std::vector<const SampleCollectionAsset*> sampleCollections;
  CollectionResolutionDiagnostics diagnostics;
  std::vector<AssetId> sampleIds;
  for (const auto& instrument : instrumentSet->instruments) {
    for (const auto& region : instrument.regions) {
      if (!region.sample.collection || !region.sample.collection->valid() ||
          std::ranges::find(sampleIds, *region.sample.collection) != sampleIds.end()) {
        continue;
      }
      const AssetId sampleId = *region.sample.collection;
      sampleIds.push_back(sampleId);
      if (const auto* samples = snapshot.asset<SampleCollectionAsset>(sampleId)) {
        sampleCollections.push_back(samples);
      } else {
        diagnostics.sampleCollections.push_back(exportError("Instrument set sample collection asset was not found"));
      }
    }
  }
  const auto instruments = instrumentView(instrumentSets);
  const SynthCollectionView synth{baseName, instruments, sampleCollections, diagnostics};

  return soundFont
             ? exportSoundFont2(synth, sources, formats, request, nullptr, ModulationConversionPolicy::SynthModulators)
             : exportDls(synth, sources, formats, request, nullptr, ModulationConversionPolicy::SynthModulators);
}

CollectionPlayback prepareCollectionPlayback(const SessionSnapshot& snapshot, const SourceStore& sources,
                                             CollectionId collection, const PlaybackRequest& request,
                                             const FormatRegistry& formats) {
  CollectionPlayback playback;
  const auto resolved = resolveCollection(snapshot, collection, sources, formats);
  if (!resolved.valid()) {
    playback.diagnostics = resolved.diagnostics().collection;
    return playback;
  }

  playback.collection = resolved.id();
  playback.title = resolved.baseName();
  if (resolved.sequence() != nullptr) {
    playback.sequence = resolved.sequence()->metadata.id;
    playback.assetDependencies.push_back(playback.sequence);
  }
  for (const auto& instruments : resolved.instrumentSets()) {
    if (instruments.metadata.id.valid()) {
      playback.assetDependencies.push_back(instruments.metadata.id);
    }
  }
  for (const auto* samples : resolved.sampleCollections()) {
    if (samples->metadata.id.valid()) {
      playback.assetDependencies.push_back(samples->metadata.id);
    }
  }

  const ExportRequest exportRequest{
      .sequence = request.sequence,
      .modulationScaling = ModulationScalingPolicy::FullFormatRange,
      .modulationConversion = request.modulationConversion,
      .dynamicEnvelopes = request.dynamicEnvelopes,
      .sampleFiltering = request.sampleFiltering,
  };
  CollectionResolutionDiagnostics diagnostics = resolved.diagnostics();
  std::vector<InstrumentSetAsset> instrumentSets = resolved.instrumentSets();
  auto rendering = renderCollection(resolved, exportRequest.sequence);
  std::optional<DynamicEnvelopeMaterialization> dynamicEnvelopes;
  if (exportRequest.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants && rendering.performance) {
    dynamicEnvelopes = materializeDynamicEnvelopes(*rendering.performance, instrumentSets);
    diagnostics.collection.insert(diagnostics.collection.end(), dynamicEnvelopes->diagnostics.begin(),
                                  dynamicEnvelopes->diagnostics.end());
  }
  const auto* preparedPerformance =
      dynamicEnvelopes ? &dynamicEnvelopes->performance : (rendering.performance ? &*rendering.performance : nullptr);
  const auto instruments = instrumentView(instrumentSets);
  auto loweredMidi = renderMidi(rendering, instruments, exportRequest.sequence.midi, exportRequest.modulationConversion,
                                preparedPerformance);
  auto midi = exportMidi(resolved.baseName(), rendering, loweredMidi, exportRequest.modulationScaling,
                         exportRequest.modulationConversion);
  const auto synthConversion = loweredMidi ? request.modulationConversion : ModulationConversionPolicy::SynthModulators;
  if (synthConversion == ModulationConversionPolicy::SynthModulators && rendering.modulation.hasSynthModulation()) {
    for (auto& instrumentSet : instrumentSets) {
      applySequenceModulation(instrumentSet, rendering.modulation);
    }
  }
  const SynthCollectionView synth{resolved.baseName(), instruments, resolved.sampleCollections(), diagnostics};
  auto soundFont = exportSoundFont2(synth, sources, formats, exportRequest, nullptr, synthConversion);

  playback.midi = std::move(midi.bytes);
  playback.soundFont = std::move(soundFont.bytes);
  playback.diagnostics = diagnostics.collection;
  playback.diagnostics.insert(playback.diagnostics.end(), std::make_move_iterator(midi.diagnostics.begin()),
                              std::make_move_iterator(midi.diagnostics.end()));
  playback.diagnostics.insert(playback.diagnostics.end(), std::make_move_iterator(soundFont.diagnostics.begin()),
                              std::make_move_iterator(soundFont.diagnostics.end()));
  if (rendering.performance) {
    playback.performance = std::move(*rendering.performance);
  }
  return playback;
}

std::vector<Artifact> exportCollection(const SessionSnapshot& snapshot, const SourceStore& sources,
                                       CollectionId collection, const ExportRequest& request,
                                       const FormatRegistry& formats) {
  const auto resolved = resolveCollection(snapshot, collection, sources, formats);
  if (!resolved.valid()) {
    return {Artifact{
        .filename = "export-error.txt",
        .mediaType = "text/plain",
        .diagnostics = resolved.diagnostics().collection,
    }};
  }

  CollectionResolutionDiagnostics diagnostics = resolved.diagnostics();
  std::vector<InstrumentSetAsset> instrumentSets = resolved.instrumentSets();
  const auto kinds = requestedKinds(request);
  const bool exportsMidi = std::ranges::find(kinds, ExportKind::Midi) != kinds.end();
  const bool exportsSynth = std::ranges::any_of(
      kinds, [](ExportKind kind) { return kind == ExportKind::SoundFont2 || kind == ExportKind::Dls; });
  const bool usesSequenceModulation = exportsSynth && resolved.sequence() != nullptr &&
                                      sequenceUsesSemantic(resolved.sequence()->program, SequenceSemantic::Modulation);
  const bool needsRendering =
      exportsMidi || (exportsSynth && (request.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants ||
                                       request.exportOnlyUsedInstruments || usesSequenceModulation));

  std::optional<RenderedCollection> rendering;
  if (needsRendering) {
    rendering = renderCollection(resolved, request.sequence);
  }

  const PerformanceSequence* preparedPerformance =
      rendering && rendering->performance ? &*rendering->performance : nullptr;
  std::optional<DynamicEnvelopeMaterialization> dynamicEnvelopes;
  if (request.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants && preparedPerformance != nullptr) {
    dynamicEnvelopes = materializeDynamicEnvelopes(*preparedPerformance, instrumentSets);
    diagnostics.collection.insert(diagnostics.collection.end(), dynamicEnvelopes->diagnostics.begin(),
                                  dynamicEnvelopes->diagnostics.end());
    preparedPerformance = &dynamicEnvelopes->performance;
  }

  const auto instruments = instrumentView(instrumentSets);
  std::optional<MidiSequence> loweredMidi;
  if (exportsMidi) {
    loweredMidi =
        renderMidi(*rendering, instruments, request.sequence.midi, request.modulationConversion, preparedPerformance);
  }

  ModulationConversionPolicy synthConversion = request.modulationConversion;
  // Sequence-event simulation replaces native synth modulation only when a
  // companion MIDI artifact was requested and could actually be rendered.
  if (synthConversion == ModulationConversionPolicy::SequenceEventSimulation && (!exportsMidi || !loweredMidi)) {
    synthConversion = ModulationConversionPolicy::SynthModulators;
  }

  std::optional<MidiModulationUsage> midiUsage;
  if (exportsSynth && synthConversion == ModulationConversionPolicy::SynthModulators && usesSequenceModulation) {
    if (rendering->modulation.hasSynthModulation()) {
      for (auto& instrumentSet : instrumentSets) {
        applySequenceModulation(instrumentSet, rendering->modulation);
      }
    }
    if (request.modulationScaling == ModulationScalingPolicy::ObservedSequenceRange) {
      midiUsage = midiModulationUsage(*rendering);
    }
  }

  const PerformanceSequence* sequenceUsage = request.exportOnlyUsedInstruments ? preparedPerformance : nullptr;
  const MidiModulationUsage* observedUsage = midiUsage ? &*midiUsage : nullptr;
  const auto writeSynth = [&](SynthExportFormat format) {
    const bool soundFont = format == SynthExportFormat::SoundFont2;
    const std::string_view extension = soundFont ? ".sf2" : ".dls";
    const std::string_view mediaType = soundFont ? "audio/soundfont" : "audio/dls";
    if (request.exportOnlyUsedInstruments && sequenceUsage == nullptr) {
      return synthArtifact(SynthCollectionView{resolved.baseName(), {}, {}, diagnostics},
                           SynthExportResult{.diagnostics = rendering->diagnostics}, extension, mediaType);
    }

    const SynthCollectionView synth{resolved.baseName(), instruments, resolved.sampleCollections(), diagnostics};
    return soundFont ? exportSoundFont2(synth, sources, formats, request, observedUsage, synthConversion, sequenceUsage)
                     : exportDls(synth, sources, formats, request, observedUsage, synthConversion, sequenceUsage);
  };

  std::vector<Artifact> artifacts;

  for (const auto kind : kinds) {
    switch (kind) {
      case ExportKind::Midi:
        artifacts.push_back(exportMidi(resolved.baseName(), *rendering, loweredMidi, request.modulationScaling,
                                       request.modulationConversion));
        break;
      case ExportKind::Wav: {
        auto wavArtifacts = exportWav(resolved, sources);
        artifacts.insert(artifacts.end(), std::make_move_iterator(wavArtifacts.begin()),
                         std::make_move_iterator(wavArtifacts.end()));
        break;
      }
      case ExportKind::SoundFont2:
        artifacts.push_back(writeSynth(SynthExportFormat::SoundFont2));
        break;
      case ExportKind::Dls:
        artifacts.push_back(writeSynth(SynthExportFormat::Dls));
        break;
    }
  }

  for (auto& artifact : artifacts) {
    artifact.diagnostics.insert(artifact.diagnostics.begin(), diagnostics.collection.begin(),
                                diagnostics.collection.end());
  }
  return artifacts;
}

std::vector<CollectionExport> exportAllCollections(const SessionSnapshot& snapshot, const SourceStore& sources,
                                                   const ExportRequest& request, const FormatRegistry& formats) {
  std::vector<CollectionExport> exports;
  exports.reserve(snapshot.collections().size());
  for (const auto& collection : snapshot.collections()) {
    exports.push_back(CollectionExport{
        .collection = collection.id,
        .artifacts = exportCollection(snapshot, sources, collection.id, request, formats),
    });
  }
  return exports;
}

}  // namespace vgmtrans::core
