/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/Export.h"

#include "value/export/CollectionPreparation.h"
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

[[nodiscard]] std::vector<Artifact> exportWav(const PreparedCollection& prepared, const SourceStore& sources) {
  if (prepared.collection->members.sampleCollections.empty()) {
    return {Artifact{
        .filename = filenamePart(prepared.baseName) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {exportError("Collection does not reference a sample collection asset")},
    }};
  }

  std::vector<Artifact> artifacts;
  u32 sampleIndex = 0;

  for (const auto& diagnostic : prepared.diagnostics.sampleCollections) {
    artifacts.push_back(Artifact{
        .filename = filenamePart(prepared.baseName) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {diagnostic},
    });
  }

  for (const auto* sampleCollection : prepared.sampleCollections) {
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
        .filename = filenamePart(prepared.baseName) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {exportError("Collection sample collection assets did not contain samples")},
    });
  }

  return artifacts;
}

[[nodiscard]] SynthExportInput synthExportInput(const PreparedCollection& prepared,
                                                std::span<const InstrumentSetAsset* const> instrumentSets,
                                                const ExportRequest& request, const FormatRegistry& formats,
                                                const MidiModulationUsage* midiModulation,
                                                ModulationConversionPolicy modulationConversion,
                                                const PerformanceSequence* sequenceUsage) {
  return SynthExportInput{
      .name = prepared.baseName,
      .instrumentSets = instrumentSets,
      .sampleCollections = prepared.sampleCollections,
      .formats = &formats,
      .sequenceUsage = sequenceUsage,
      .midiModulationUsage = midiModulation,
      .modulationScaling = request.modulationScaling,
      .modulationConversion = modulationConversion,
      .sampleFiltering = request.sampleFiltering,
  };
}

[[nodiscard]] Artifact synthArtifact(const PreparedCollection& prepared, SynthExportResult result,
                                     std::string_view extension, std::string_view mediaType) {
  auto diagnostics = prepared.diagnostics.instrumentSets;
  diagnostics.insert(diagnostics.end(), prepared.diagnostics.sampleCollections.begin(),
                     prepared.diagnostics.sampleCollections.end());
  diagnostics.insert(diagnostics.end(), std::make_move_iterator(result.diagnostics.begin()),
                     std::make_move_iterator(result.diagnostics.end()));

  return Artifact{
      .filename = filenamePart(prepared.baseName) + std::string(extension),
      .mediaType = std::string(mediaType),
      .bytes = std::move(result.bytes),
      .diagnostics = std::move(diagnostics),
  };
}

[[nodiscard]] Artifact exportSoundFont2(const PreparedCollection& prepared, const SourceStore& sources,
                                        const FormatRegistry& formats, const ExportRequest& request,
                                        const MidiModulationUsage* midiModulation,
                                        ModulationConversionPolicy modulationConversion,
                                        const PerformanceSequence* sequenceUsage = nullptr) {
  const auto instruments = prepared.instrumentView();
  const auto input =
      synthExportInput(prepared, instruments, request, formats, midiModulation, modulationConversion, sequenceUsage);
  return synthArtifact(prepared, buildSoundFont2(input, sources), ".sf2", "audio/soundfont");
}

[[nodiscard]] Artifact exportDls(const PreparedCollection& prepared, const SourceStore& sources,
                                 const FormatRegistry& formats, const ExportRequest& request,
                                 const MidiModulationUsage* midiModulation,
                                 ModulationConversionPolicy modulationConversion,
                                 const PerformanceSequence* sequenceUsage = nullptr) {
  const auto instruments = prepared.instrumentView();
  const auto input =
      synthExportInput(prepared, instruments, request, formats, midiModulation, modulationConversion, sequenceUsage);
  return synthArtifact(prepared, buildDls(input, sources), ".dls", "audio/dls");
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

  const auto rendering = renderSequence(*sequence, formats, request);
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

  PreparedCollection prepared;
  prepared.baseName = baseName;
  prepared.instrumentSets.push_back(*instrumentSet);
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
        prepared.sampleCollections.push_back(samples);
      } else {
        prepared.diagnostics.sampleCollections.push_back(
            exportError("Instrument set sample collection asset was not found"));
      }
    }
  }

  return soundFont
             ? exportSoundFont2(prepared, sources, formats, request, nullptr,
                                ModulationConversionPolicy::SynthModulators)
             : exportDls(prepared, sources, formats, request, nullptr, ModulationConversionPolicy::SynthModulators);
}

CollectionPlayback prepareCollectionPlayback(const SessionSnapshot& snapshot, const SourceStore& sources,
                                             CollectionId collection, const PlaybackRequest& request,
                                             const FormatRegistry& formats) {
  CollectionPlayback playback{
      .collection = collection,
  };
  auto prepared = prepareCollection(snapshot, collection, sources, formats);
  if (prepared.collection == nullptr) {
    playback.diagnostics = prepared.diagnostics.collection;
    return playback;
  }

  playback.title = prepared.baseName;
  if (prepared.sequence != nullptr) {
    playback.sequence = prepared.sequence->metadata.id;
    playback.assetDependencies.push_back(playback.sequence);
  }
  playback.assetDependencies.insert(playback.assetDependencies.end(),
                                    prepared.collection->members.instrumentSets.begin(),
                                    prepared.collection->members.instrumentSets.end());
  playback.assetDependencies.insert(playback.assetDependencies.end(),
                                    prepared.collection->members.sampleCollections.begin(),
                                    prepared.collection->members.sampleCollections.end());

  const ExportRequest exportRequest{
      .sequence = request.sequence,
      .modulationScaling = ModulationScalingPolicy::FullFormatRange,
      .modulationConversion = request.modulationConversion,
      .dynamicEnvelopes = request.dynamicEnvelopes,
      .sampleFiltering = request.sampleFiltering,
  };
  auto rendering = renderCollection(prepared, formats, exportRequest.sequence);
  auto dynamicEnvelopes = materializeCollectionDynamicEnvelopes(prepared, rendering, exportRequest.dynamicEnvelopes);
  const auto* preparedPerformance =
      dynamicEnvelopes ? &dynamicEnvelopes->performance : (rendering.performance ? &*rendering.performance : nullptr);
  const auto instruments = prepared.instrumentView();
  auto loweredMidi = renderMidi(rendering, instruments, exportRequest.sequence.midi, exportRequest.modulationConversion,
                                preparedPerformance);
  auto midi = exportMidi(prepared.baseName, rendering, loweredMidi, exportRequest.modulationScaling,
                         exportRequest.modulationConversion);
  const auto synthConversion = loweredMidi ? request.modulationConversion : ModulationConversionPolicy::SynthModulators;
  if (synthConversion == ModulationConversionPolicy::SynthModulators) {
    applyCollectionSequenceModulation(prepared, rendering.modulation);
  }
  auto soundFont = exportSoundFont2(prepared, sources, formats, exportRequest, nullptr, synthConversion);

  playback.midi = std::move(midi.bytes);
  playback.soundFont = std::move(soundFont.bytes);
  playback.diagnostics = prepared.diagnostics.collection;
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
  auto prepared = prepareCollection(snapshot, collection, sources, formats);
  if (prepared.collection == nullptr) {
    return {Artifact{
        .filename = "export-error.txt",
        .mediaType = "text/plain",
        .diagnostics = prepared.diagnostics.collection,
    }};
  }

  std::vector<Artifact> artifacts;
  std::optional<RenderedCollection> rendering;
  std::optional<MidiSequence> loweredMidi;
  bool midiRendered = false;
  std::optional<MidiModulationUsage> midiUsage;
  bool midiUsageAnalyzed = false;
  bool sequenceModulationApplied = false;
  std::optional<DynamicEnvelopeMaterialization> dynamicEnvelopes;
  bool dynamicEnvelopesMaterialized = false;
  std::optional<bool> sequenceHasModulation;
  const auto kinds = requestedKinds(request);
  const bool exportsMidi = std::ranges::find(kinds, ExportKind::Midi) != kinds.end();

  const auto requireRendering = [&]() -> const RenderedCollection& {
    if (!rendering) {
      rendering = renderCollection(prepared, formats, request.sequence);
    }
    return *rendering;
  };

  const auto requirePreparedPerformance = [&]() -> const PerformanceSequence* {
    const auto& rendered = requireRendering();
    if (!dynamicEnvelopesMaterialized) {
      if (request.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants) {
        dynamicEnvelopes = materializeCollectionDynamicEnvelopes(prepared, rendered, request.dynamicEnvelopes);
      }
      dynamicEnvelopesMaterialized = true;
    }
    if (dynamicEnvelopes) {
      return &dynamicEnvelopes->performance;
    }
    return rendered.performance ? &*rendered.performance : nullptr;
  };

  const auto requireMidi = [&]() -> const std::optional<MidiSequence>& {
    if (!midiRendered) {
      const auto* performance = requirePreparedPerformance();
      const auto instruments = prepared.instrumentView();
      loweredMidi =
          renderMidi(requireRendering(), instruments, request.sequence.midi, request.modulationConversion, performance);
      midiRendered = true;
    }
    return loweredMidi;
  };

  const auto requireSequenceModulation = [&]() {
    if (sequenceModulationApplied) {
      return;
    }
    applyCollectionSequenceModulation(prepared, requireRendering().modulation);
    sequenceModulationApplied = true;
  };

  const auto usesSequenceModulation = [&]() {
    if (!sequenceHasModulation) {
      sequenceHasModulation = prepared.sequence != nullptr &&
                              sequenceUsesSemantic(prepared.sequence->program, SequenceSemantic::Modulation);
    }
    return *sequenceHasModulation;
  };

  const auto requireMidiModulationUsage = [&]() -> const MidiModulationUsage* {
    // Synth exporters only need observed MIDI modulation when the policy asks for it.
    // WAV and plain MIDI export should not pay that analysis cost.
    if (request.modulationScaling != ModulationScalingPolicy::ObservedSequenceRange) {
      return nullptr;
    }
    if (!usesSequenceModulation()) {
      return nullptr;
    }
    if (!midiUsageAnalyzed) {
      midiUsage = midiModulationUsage(requireRendering());
      midiUsageAnalyzed = true;
    }
    return midiUsage ? &*midiUsage : nullptr;
  };

  const auto requireSequenceUsage = [&]() -> const PerformanceSequence* {
    return request.exportOnlyUsedInstruments ? requirePreparedPerformance() : nullptr;
  };

  const auto synthModulationConversion = [&]() {
    if (request.modulationConversion == ModulationConversionPolicy::SynthModulators) {
      return ModulationConversionPolicy::SynthModulators;
    }
    // Sequence-event simulation is a replacement, not a reason to discard an
    // instrument's modulation. Keep native modulation unless a MIDI artifact
    // was requested and successfully written alongside the synth file.
    if (!exportsMidi || !requireMidi()) {
      return ModulationConversionPolicy::SynthModulators;
    }
    return ModulationConversionPolicy::SequenceEventSimulation;
  };

  for (const auto kind : kinds) {
    switch (kind) {
      case ExportKind::Midi:
        artifacts.push_back(exportMidi(prepared.baseName, requireRendering(), requireMidi(), request.modulationScaling,
                                       request.modulationConversion));
        break;
      case ExportKind::Wav: {
        auto wavArtifacts = exportWav(prepared, sources);
        artifacts.insert(artifacts.end(), std::make_move_iterator(wavArtifacts.begin()),
                         std::make_move_iterator(wavArtifacts.end()));
        break;
      }
      case ExportKind::SoundFont2: {
        if (request.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants) {
          static_cast<void>(requirePreparedPerformance());
        }
        const auto* sequenceUsage = requireSequenceUsage();
        if (request.exportOnlyUsedInstruments && sequenceUsage == nullptr) {
          artifacts.push_back(synthArtifact(prepared, SynthExportResult{.diagnostics = requireRendering().diagnostics},
                                            ".sf2", "audio/soundfont"));
          break;
        }
        const auto conversion = synthModulationConversion();
        if (conversion == ModulationConversionPolicy::SynthModulators && usesSequenceModulation()) {
          requireSequenceModulation();
        }
        artifacts.push_back(exportSoundFont2(
            prepared, sources, formats, request,
            conversion == ModulationConversionPolicy::SynthModulators ? requireMidiModulationUsage() : nullptr,
            conversion, sequenceUsage));
        break;
      }
      case ExportKind::Dls: {
        if (request.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants) {
          static_cast<void>(requirePreparedPerformance());
        }
        const auto* sequenceUsage = requireSequenceUsage();
        if (request.exportOnlyUsedInstruments && sequenceUsage == nullptr) {
          artifacts.push_back(synthArtifact(prepared, SynthExportResult{.diagnostics = requireRendering().diagnostics},
                                            ".dls", "audio/dls"));
          break;
        }
        const auto conversion = synthModulationConversion();
        if (conversion == ModulationConversionPolicy::SynthModulators && usesSequenceModulation()) {
          requireSequenceModulation();
        }
        artifacts.push_back(exportDls(
            prepared, sources, formats, request,
            conversion == ModulationConversionPolicy::SynthModulators ? requireMidiModulationUsage() : nullptr,
            conversion, sequenceUsage));
        break;
      }
    }
  }

  for (auto& artifact : artifacts) {
    artifact.diagnostics.insert(artifact.diagnostics.begin(), prepared.diagnostics.collection.begin(),
                                prepared.diagnostics.collection.end());
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
