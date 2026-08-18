/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/Export.h"

#include "value/export/CollectionBinding.h"
#include "value/export/DynamicEnvelope.h"
#include "value/export/ExportDiagnostics.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/ModulationAnalysis.h"
#include "value/export/synth/SynthExportData.h"
#include "value/model/SessionSnapshot.h"
#include "value/synth/SampleDecoder.h"
#include "value/validation/SynthValidation.h"
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

[[nodiscard]] std::string artifactBaseName(const AssetMetadata& metadata, std::string_view fallback) {
  if (!metadata.name.empty()) {
    return filenamePart(metadata.name);
  }
  return std::string(fallback) + "-" + std::to_string(metadata.id.value);
}

[[nodiscard]] std::string sampleArtifactName(std::string_view baseName, const Sample& sample, u32 sampleIndex) {
  std::string sampleName = sample.name.empty() ? "sample-" + std::to_string(sampleIndex) : sample.name;
  return filenamePart(std::string(baseName)) + "-" + std::to_string(sampleIndex) + "-" +
         filenamePart(std::move(sampleName)) + ".wav";
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
                                                     std::span<const SoundBankAsset* const> soundBanks,
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
  return renderMidiSequence(*performance, options, modulationConversion, soundBanks, &rendering.modulation);
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

[[nodiscard]] std::vector<const SoundBankAsset*> bankView(std::span<const SoundBankAsset> soundBanks) {
  std::vector<const SoundBankAsset*> view;
  view.reserve(soundBanks.size());
  for (const auto& bank : soundBanks) {
    view.push_back(&bank);
  }
  return view;
}

struct SynthCollectionView {
  std::string_view name;
  std::span<const SoundBankAsset* const> soundBanks;
  std::span<const SamplePoolAsset* const> samplePools;
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

void appendWavArtifacts(std::vector<Artifact>& artifacts, std::string_view baseName, const SamplePool& pool,
                        const SourceStore& sources) {
  for (const auto& sample : pool.samples) {
    const auto sampleIndex = static_cast<u32>(artifacts.size());
    Artifact artifact{
        .filename = sampleArtifactName(baseName, sample, sampleIndex),
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

[[nodiscard]] std::vector<Artifact> exportWav(const BoundCollection& collection, const SourceStore& sources) {
  std::vector<Artifact> artifacts;
  for (const auto& bank : collection.soundBanks()) {
    appendWavArtifacts(artifacts, collection.baseName(), bank.localSamples, sources);
  }
  for (const auto* samplePool : collection.samplePools()) {
    appendWavArtifacts(artifacts, collection.baseName(), samplePool->pool, sources);
  }

  if (artifacts.empty()) {
    artifacts.push_back(Artifact{
        .filename = filenamePart(collection.baseName()) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {exportError("Collection sound banks and sample pools did not contain samples")},
    });
  }

  return artifacts;
}

[[nodiscard]] SynthExportInput synthExportInput(const SynthCollectionView& collection, const ExportRequest& request,
                                                const MidiModulationUsage* midiModulation,
                                                ModulationConversionPolicy modulationConversion,
                                                const PerformanceSequence* sequenceUsage) {
  return SynthExportInput{
      .name = std::string(collection.name),
      .soundBanks = collection.soundBanks,
      .samplePools = collection.samplePools,
      .sequenceUsage = sequenceUsage,
      .midiModulationUsage = midiModulation,
      .modulationScaling = request.modulationScaling,
      .modulationConversion = modulationConversion,
      .sampleFiltering = request.sampleFiltering,
  };
}

[[nodiscard]] Artifact synthArtifact(const SynthCollectionView& collection, SynthExportResult result,
                                     std::string_view extension, std::string_view mediaType) {
  return Artifact{
      .filename = filenamePart(std::string(collection.name)) + std::string(extension),
      .mediaType = std::string(mediaType),
      .bytes = std::move(result.bytes),
      .diagnostics = std::move(result.diagnostics),
  };
}

[[nodiscard]] Artifact exportSoundFont2(const SynthCollectionView& collection, const SourceStore& sources,
                                        const ExportRequest& request, const MidiModulationUsage* midiModulation,
                                        ModulationConversionPolicy modulationConversion,
                                        const PerformanceSequence* sequenceUsage = nullptr) {
  const auto input = synthExportInput(collection, request, midiModulation, modulationConversion, sequenceUsage);
  return synthArtifact(collection, buildSoundFont2(input, sources), ".sf2", "audio/soundfont");
}

[[nodiscard]] Artifact exportDls(const SynthCollectionView& collection, const SourceStore& sources,
                                 const ExportRequest& request, const MidiModulationUsage* midiModulation,
                                 ModulationConversionPolicy modulationConversion,
                                 const PerformanceSequence* sequenceUsage = nullptr) {
  const auto input = synthExportInput(collection, request, midiModulation, modulationConversion, sequenceUsage);
  return synthArtifact(collection, buildDls(input, sources), ".dls", "audio/dls");
}

Artifact exportStandaloneSequenceMidi(const SessionSnapshot& snapshot, AssetId sequenceId,
                                      const SequenceExportRequest& request) {
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
  return exportMidi(artifactBaseName(sequence->metadata, "sequence"), rendering, midi,
                    ModulationScalingPolicy::FullFormatRange,
                    ModulationConversionPolicy::SequenceEventSimulation);
}

}  // namespace

Artifact exportSequenceMidi(const SessionSnapshot& snapshot, const SourceStore& sources, AssetId sequenceId,
                            const SequenceExportRequest& request) {
  const auto* sequence = snapshot.asset<SequenceProgramAsset>(sequenceId);
  const auto* collection = snapshot.firstCollectionContaining(sequenceId);
  if (sequence == nullptr || collection == nullptr) {
    return exportStandaloneSequenceMidi(snapshot, sequenceId, request);
  }

  auto artifacts = exportCollection(snapshot, sources, collection->id,
                                    ExportRequest{
                                        .kinds = {ExportKind::Midi},
                                        .sequence = request,
                                        .modulationScaling = ModulationScalingPolicy::FullFormatRange,
                                        .modulationConversion = ModulationConversionPolicy::SequenceEventSimulation,
                                    });
  if (artifacts.empty()) {
    return Artifact{
        .filename = artifactBaseName(sequence->metadata, "sequence") + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = {exportError("Collection MIDI export produced no artifact")},
    };
  }
  artifacts.front().filename = artifactBaseName(sequence->metadata, "sequence") + ".mid";
  return std::move(artifacts.front());
}

Artifact exportSoundBank(const SessionSnapshot& snapshot, const SourceStore& sources, AssetId soundBankId,
                         SynthExportFormat format, const ExportRequest& request) {
  const bool soundFont = format == SynthExportFormat::SoundFont2;
  const ExportKind kind = soundFont ? ExportKind::SoundFont2 : ExportKind::Dls;
  const std::string extension = soundFont ? ".sf2" : ".dls";
  const std::string mediaType = soundFont ? "audio/soundfont" : "audio/dls";
  const auto* asset = snapshot.asset(soundBankId);
  const auto* soundBank = asset != nullptr ? std::get_if<SoundBankAsset>(asset) : nullptr;
  if (soundBank == nullptr) {
    return Artifact{
        .filename = "sound-bank-" + std::to_string(soundBankId.value) + extension,
        .mediaType = mediaType,
        .diagnostics = {exportError(asset == nullptr ? "Sound bank asset was not found" : "Asset is not a sound bank")},
    };
  }

  const std::string baseName = artifactBaseName(soundBank->metadata, "sound-bank");
  const auto failedArtifact = [&](std::vector<Diagnostic> diagnostics) {
    return Artifact{
        .filename = baseName + extension,
        .mediaType = mediaType,
        .diagnostics = std::move(diagnostics),
    };
  };
  const size_t collectionCount = snapshot.countCollectionsContaining(soundBankId);
  if (collectionCount > 1) {
    return failedArtifact(
        {exportError("Sound bank belongs to multiple collections; export a specific collection instead",
                     validDiagnosticRange(soundBank->metadata.range))});
  }
  if (collectionCount == 1) {
    const auto* collection = snapshot.firstCollectionContaining(soundBankId);
    auto collectionRequest = request;
    collectionRequest.kinds = {kind};
    auto artifacts = exportCollection(snapshot, sources, collection->id, collectionRequest);
    if (!artifacts.empty()) {
      artifacts.front().filename = baseName + extension;
      return std::move(artifacts.front());
    }
    return failedArtifact({exportError("Collection sound bank export produced no artifact")});
  }

  std::vector<const SamplePoolAsset*> samplePools;
  for (const auto& instrument : soundBank->instruments) {
    for (const auto& region : instrument.regions) {
      if (!region.sample.owner.valid() || region.sample.owner == soundBankId) {
        continue;
      }
      const auto alreadySelected = std::ranges::find(samplePools, region.sample.owner,
                                                     [](const SamplePoolAsset* pool) { return pool->metadata.id; });
      if (alreadySelected == samplePools.end()) {
        if (const auto* samples = snapshot.asset<SamplePoolAsset>(region.sample.owner)) {
          samplePools.push_back(samples);
        }
      }
    }
  }
  auto validation = validateSampleReferences(*soundBank, samplePools);
  if (!validation.empty()) {
    return failedArtifact(validation.takeDiagnostics());
  }
  std::vector<SoundBankAsset> soundBanks{*soundBank};
  const auto banks = bankView(soundBanks);
  const SynthCollectionView synth{baseName, banks, samplePools};

  auto artifact = soundFont
                      ? exportSoundFont2(synth, sources, request, nullptr, ModulationConversionPolicy::SynthModulators)
                      : exportDls(synth, sources, request, nullptr, ModulationConversionPolicy::SynthModulators);
  return artifact;
}

std::vector<Artifact> exportSamples(const SessionSnapshot& snapshot, const SourceStore& sources, AssetId ownerId) {
  const auto* asset = snapshot.asset(ownerId);
  const SamplePool* pool = nullptr;
  std::string baseName;
  if (const auto* bank = asset != nullptr ? std::get_if<SoundBankAsset>(asset) : nullptr) {
    pool = &bank->localSamples;
    baseName = artifactBaseName(bank->metadata, "sound-bank");
  } else if (const auto* samples = asset != nullptr ? std::get_if<SamplePoolAsset>(asset) : nullptr) {
    pool = &samples->pool;
    baseName = artifactBaseName(samples->metadata, "samples");
  }

  std::vector<Artifact> artifacts;
  if (pool != nullptr) {
    appendWavArtifacts(artifacts, baseName, *pool, sources);
  }
  if (artifacts.empty()) {
    artifacts.push_back(Artifact{
        .filename = (baseName.empty() ? "samples-" + std::to_string(ownerId.value) : baseName) + "-samples.wav",
        .mediaType = "audio/wav",
        .diagnostics = {exportError(asset == nullptr              ? "Sample owner asset was not found"
                                    : pool == nullptr             ? "Asset does not contain samples"
                                                                  : "Asset does not contain any samples")},
    });
  }
  return artifacts;
}

CollectionPlayback prepareCollectionPlayback(const SessionSnapshot& snapshot, const SourceStore& sources,
                                             CollectionId collection, const PlaybackRequest& request) {
  CollectionPlayback playback;
  auto binding = bindCollection(snapshot, collection);
  if (!binding.collection) {
    playback.diagnostics = std::move(binding.diagnostics);
    return playback;
  }
  const auto& bound = *binding.collection;

  playback.collection = bound.id();
  playback.title = bound.baseName();
  if (const auto sequence = bound.sequenceId()) {
    playback.sequence = *sequence;
    playback.assetDependencies.push_back(playback.sequence);
  }
  for (const auto& instruments : bound.soundBanks()) {
    if (instruments.metadata.id.valid()) {
      playback.assetDependencies.push_back(instruments.metadata.id);
    }
  }
  for (const auto* samples : bound.samplePools()) {
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
  std::vector<Diagnostic> diagnostics = binding.diagnostics;
  std::vector<SoundBankAsset> soundBanks = bound.soundBanks();
  auto rendering = renderCollection(bound, exportRequest.sequence);
  std::optional<DynamicEnvelopeMaterialization> dynamicEnvelopes;
  if (exportRequest.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants && rendering.performance) {
    dynamicEnvelopes = materializeDynamicEnvelopes(*rendering.performance, soundBanks);
    diagnostics.insert(diagnostics.end(), dynamicEnvelopes->diagnostics.begin(), dynamicEnvelopes->diagnostics.end());
  }
  const auto* preparedPerformance =
      dynamicEnvelopes ? &dynamicEnvelopes->performance : (rendering.performance ? &*rendering.performance : nullptr);
  const auto instruments = bankView(soundBanks);
  auto loweredMidi = renderMidi(rendering, instruments, exportRequest.sequence.midi, exportRequest.modulationConversion,
                                preparedPerformance);
  auto midi = exportMidi(bound.baseName(), rendering, loweredMidi, exportRequest.modulationScaling,
                         exportRequest.modulationConversion);
  const auto synthConversion = loweredMidi ? request.modulationConversion : ModulationConversionPolicy::SynthModulators;
  if (synthConversion == ModulationConversionPolicy::SynthModulators && rendering.modulation.hasSynthModulation()) {
    for (auto& soundBank : soundBanks) {
      applySequenceModulation(soundBank, rendering.modulation);
    }
  }
  const SynthCollectionView synth{bound.baseName(), instruments, bound.samplePools()};
  auto soundFont = exportSoundFont2(synth, sources, exportRequest, nullptr, synthConversion);

  playback.midi = std::move(midi.bytes);
  playback.soundFont = std::move(soundFont.bytes);
  playback.diagnostics = std::move(diagnostics);
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
                                       CollectionId collection, const ExportRequest& request) {
  auto binding = bindCollection(snapshot, collection);
  if (!binding.collection) {
    return {Artifact{
        .filename = "export-error.txt",
        .mediaType = "text/plain",
        .diagnostics = std::move(binding.diagnostics),
    }};
  }
  const auto& bound = *binding.collection;

  std::vector<Diagnostic> diagnostics = binding.diagnostics;
  std::vector<SoundBankAsset> soundBanks = bound.soundBanks();
  const auto kinds = requestedKinds(request);
  const bool exportsMidi = std::ranges::find(kinds, ExportKind::Midi) != kinds.end();
  const bool exportsSynth = std::ranges::any_of(
      kinds, [](ExportKind kind) { return kind == ExportKind::SoundFont2 || kind == ExportKind::Dls; });
  const bool synthRequiresPerformance =
      request.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants || request.exportOnlyUsedInstruments;
  const bool needsRendering = exportsMidi || (exportsSynth && (bound.hasSequence() || synthRequiresPerformance));

  RenderedCollection rendering;
  if (needsRendering) {
    rendering = renderCollection(bound, request.sequence);
  }

  const PerformanceSequence* preparedPerformance = rendering.performance ? &*rendering.performance : nullptr;
  std::optional<DynamicEnvelopeMaterialization> dynamicEnvelopes;
  if (request.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants && preparedPerformance != nullptr) {
    dynamicEnvelopes = materializeDynamicEnvelopes(*preparedPerformance, soundBanks);
    diagnostics.insert(diagnostics.end(), dynamicEnvelopes->diagnostics.begin(), dynamicEnvelopes->diagnostics.end());
    preparedPerformance = &dynamicEnvelopes->performance;
  }

  const auto instruments = bankView(soundBanks);
  std::optional<MidiSequence> loweredMidi;
  if (exportsMidi) {
    loweredMidi =
        renderMidi(rendering, instruments, request.sequence.midi, request.modulationConversion, preparedPerformance);
  }

  ModulationConversionPolicy synthConversion = request.modulationConversion;
  // Sequence-event simulation replaces native synth modulation only when a
  // companion MIDI artifact was requested and could actually be rendered.
  if (synthConversion == ModulationConversionPolicy::SequenceEventSimulation && (!exportsMidi || !loweredMidi)) {
    synthConversion = ModulationConversionPolicy::SynthModulators;
  }

  std::optional<MidiModulationUsage> midiUsage;
  if (exportsSynth && rendering.performance && synthConversion == ModulationConversionPolicy::SynthModulators) {
    if (rendering.modulation.hasSynthModulation()) {
      for (auto& soundBank : soundBanks) {
        applySequenceModulation(soundBank, rendering.modulation);
      }
    }
    if (request.modulationScaling == ModulationScalingPolicy::ObservedSequenceRange) {
      midiUsage = midiModulationUsage(rendering);
    }
  }

  const PerformanceSequence* sequenceUsage = request.exportOnlyUsedInstruments ? preparedPerformance : nullptr;
  const MidiModulationUsage* observedUsage = midiUsage ? &*midiUsage : nullptr;
  const auto writeSynth = [&](SynthExportFormat format) {
    const bool soundFont = format == SynthExportFormat::SoundFont2;
    const std::string_view extension = soundFont ? ".sf2" : ".dls";
    const std::string_view mediaType = soundFont ? "audio/soundfont" : "audio/dls";
    if (synthRequiresPerformance && preparedPerformance == nullptr) {
      return synthArtifact(SynthCollectionView{bound.baseName(), {}, {}},
                           SynthExportResult{.diagnostics = rendering.diagnostics}, extension, mediaType);
    }

    const SynthCollectionView synth{bound.baseName(), instruments, bound.samplePools()};
    auto artifact = soundFont ? exportSoundFont2(synth, sources, request, observedUsage, synthConversion, sequenceUsage)
                              : exportDls(synth, sources, request, observedUsage, synthConversion, sequenceUsage);
    if (!rendering.performance) {
      artifact.diagnostics.insert(artifact.diagnostics.begin(), rendering.diagnostics.begin(),
                                  rendering.diagnostics.end());
    }
    return artifact;
  };

  std::vector<Artifact> artifacts;

  for (const auto kind : kinds) {
    switch (kind) {
      case ExportKind::Midi:
        artifacts.push_back(exportMidi(bound.baseName(), rendering, loweredMidi, request.modulationScaling,
                                       request.modulationConversion));
        break;
      case ExportKind::Wav: {
        auto wavArtifacts = exportWav(bound, sources);
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
    artifact.diagnostics.insert(artifact.diagnostics.begin(), diagnostics.begin(), diagnostics.end());
  }
  return artifacts;
}

std::vector<CollectionExport> exportAllCollections(const SessionSnapshot& snapshot, const SourceStore& sources,
                                                   const ExportRequest& request) {
  std::vector<CollectionExport> exports;
  exports.reserve(snapshot.collections().size());
  for (const auto& collection : snapshot.collections()) {
    exports.push_back(CollectionExport{
        .collection = collection.id,
        .artifacts = exportCollection(snapshot, sources, collection.id, request),
    });
  }
  return exports;
}

}  // namespace vgmtrans::core
