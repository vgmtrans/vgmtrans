/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/Export.h"

#include "value/export/CollectionBinding.h"
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
#include <array>
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

[[nodiscard]] Artifact exportMidi(std::string_view baseName, const RenderedCollection& rendering,
                                  const std::optional<MidiSequence>& midi) {
  return Artifact{
      .filename = std::string(baseName) + ".mid",
      .mediaType = "audio/midi",
      .bytes = midi ? encodeMidiFile(*midi) : std::vector<u8>{},
      .diagnostics = midi ? midi->diagnostics : rendering.diagnostics,
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
        artifact.diagnostics.push_back(exportError("Sample source was not found", sample.encodedData));
      } else if (auto decoded = decodeSample(sample, sources.bytes(sample.encodedData.source))) {
        artifact.bytes = encodePcm16Wav(*decoded);
      } else {
        artifact.diagnostics.push_back(exportError("Unsupported sample codec", sample.encodedData));
      }
    } catch (const std::exception& ex) {
      artifact.diagnostics.push_back(exportError(ex.what(), sample.encodedData));
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

[[nodiscard]] Artifact synthArtifact(std::string_view name, SynthExportFormat format, SynthExportResult result) {
  const bool soundFont = format == SynthExportFormat::SoundFont2;
  return Artifact{
      .filename = filenamePart(std::string(name)) + (soundFont ? ".sf2" : ".dls"),
      .mediaType = soundFont ? "audio/soundfont" : "audio/dls",
      .bytes = std::move(result.bytes),
      .diagnostics = std::move(result.diagnostics),
  };
}

[[nodiscard]] Artifact exportSynth(const SynthExportInput& input, SynthExportFormat format,
                                   const SourceStore& sources) {
  return synthArtifact(
      input.name, format,
      format == SynthExportFormat::SoundFont2 ? buildSoundFont2(input, sources) : buildDls(input, sources));
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
      diagnostics.push_back(exportError("Asset is not a sequence", metadata(*asset).range));
    }
    return Artifact{
        .filename = "sequence-" + std::to_string(sequenceId.value) + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = std::move(diagnostics),
    };
  }

  const auto rendering = renderSequence(*sequence, request);
  std::optional<MidiSequence> midi;
  if (rendering.performance) {
    midi = renderMidiSequence(*rendering.performance, request.midi, ModulationConversionPolicy::SequenceEventSimulation,
                              {}, &rendering.modulation);
  }
  return exportMidi(artifactBaseName(sequence->metadata, "sequence"), rendering, midi);
}

[[nodiscard]] std::vector<Artifact> exportCollectionImpl(const SessionSnapshot& snapshot, const SourceStore& sources,
                                                         CollectionId collection, const ExportRequest& request,
                                                         std::optional<AssetId> soundBank);

}  // namespace

Artifact exportSequenceMidi(const SessionSnapshot& snapshot, const SourceStore& sources, AssetId sequenceId,
                            const SequenceExportRequest& request) {
  const auto* sequence = snapshot.asset<SequenceProgramAsset>(sequenceId);
  const size_t collectionCount = snapshot.countCollectionsContaining(sequenceId);
  if (sequence == nullptr || collectionCount == 0) {
    return exportStandaloneSequenceMidi(snapshot, sequenceId, request);
  }
  if (collectionCount > 1) {
    return Artifact{
        .filename = artifactBaseName(sequence->metadata, "sequence") + ".mid",
        .mediaType = "audio/midi",
        .diagnostics = {exportError("Sequence belongs to multiple collections; export a specific collection instead",
                                    sequence->metadata.range)},
    };
  }

  auto artifacts = exportCollection(snapshot, sources, snapshot.firstCollectionContaining(sequenceId)->id,
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
  const auto* asset = snapshot.asset(soundBankId);
  const auto* soundBank = asset != nullptr ? std::get_if<SoundBankAsset>(asset) : nullptr;
  if (soundBank == nullptr) {
    return synthArtifact(
        "sound-bank-" + std::to_string(soundBankId.value), format,
        SynthExportResult{.diagnostics = {exportError(asset == nullptr ? "Sound bank asset was not found"
                                                                       : "Asset is not a sound bank")}});
  }

  const std::string baseName = artifactBaseName(soundBank->metadata, "sound-bank");
  const auto failedArtifact = [&](std::vector<Diagnostic> diagnostics) {
    return synthArtifact(baseName, format, SynthExportResult{.diagnostics = std::move(diagnostics)});
  };
  const size_t collectionCount = snapshot.countCollectionsContaining(soundBankId);
  if (request.exportOnlyUsedInstruments && collectionCount > 1) {
    return failedArtifact(
        {exportError("Sound bank belongs to multiple collections; export a specific collection instead",
                     soundBank->metadata.range)});
  }
  if (request.exportOnlyUsedInstruments && collectionCount == 1) {
    const auto* collection = snapshot.firstCollectionContaining(soundBankId);
    auto collectionRequest = request;
    collectionRequest.kinds = {kind};
    auto artifacts = exportCollectionImpl(snapshot, sources, collection->id, collectionRequest, soundBankId);
    if (!artifacts.empty()) {
      artifacts.front().filename = baseName + extension;
      return std::move(artifacts.front());
    }
    return failedArtifact({exportError("Collection sound bank export produced no artifact")});
  }

  if (request.exportOnlyUsedInstruments) {
    return failedArtifact({exportError("Used-instrument export requires a collection with a sequence")});
  }
  auto binding = bindSoundBank(snapshot, soundBankId);
  if (!binding.collection) {
    return failedArtifact(std::move(binding.diagnostics));
  }
  const std::array banks{&binding.collection->soundBanks().front()};
  auto artifact = exportSynth(
      SynthExportInput{
          .name = baseName,
          .soundBanks = banks,
          .samplePools = binding.collection->samplePools(),
          .filterSamplesToReferencedInstruments = true,
          .modulationScaling = request.modulationScaling,
          .sampleFiltering = request.sampleFiltering,
      },
      format, sources);
  artifact.diagnostics.insert(artifact.diagnostics.begin(), std::make_move_iterator(binding.diagnostics.begin()),
                              std::make_move_iterator(binding.diagnostics.end()));
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
        .diagnostics = {exportError(asset == nullptr  ? "Sample owner asset was not found"
                                    : pool == nullptr ? "Asset does not contain samples"
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
  CollectionWorkspace workspace{std::move(*binding.collection), std::move(binding.diagnostics)};
  const auto& bound = workspace.collection;

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

  workspace.render(request.sequence, request.dynamicEnvelopes, /*materializeSignedStereo=*/true);
  const auto instruments = workspace.soundBankView();
  std::optional<MidiSequence> midi;
  if (const auto* performance = workspace.performance()) {
    midi = renderMidiSequence(*performance, request.sequence.midi, request.modulationConversion, instruments,
                              &workspace.rendering.modulation);
  }
  const auto synthConversion = midi ? request.modulationConversion : ModulationConversionPolicy::SynthModulators;
  workspace.prepareModulation(synthConversion, ModulationScalingPolicy::FullFormatRange);
  auto soundFont = buildSoundFont2(
      SynthExportInput{
          .name = bound.baseName(),
          .soundBanks = instruments,
          .samplePools = bound.samplePools(),
          .modulationConversion = synthConversion,
          .sampleFiltering = request.sampleFiltering,
      },
      sources);

  if (midi) {
    playback.midi = encodeMidiFile(*midi);
  }
  playback.soundFont = std::move(soundFont.bytes);
  playback.diagnostics = std::move(workspace.diagnostics);
  auto& midiDiagnostics = midi ? midi->diagnostics : workspace.rendering.diagnostics;
  playback.diagnostics.insert(playback.diagnostics.end(), std::make_move_iterator(midiDiagnostics.begin()),
                              std::make_move_iterator(midiDiagnostics.end()));
  playback.diagnostics.insert(playback.diagnostics.end(), std::make_move_iterator(soundFont.diagnostics.begin()),
                              std::make_move_iterator(soundFont.diagnostics.end()));
  if (workspace.rendering.performance) {
    playback.performance = std::move(*workspace.rendering.performance);
  }
  return playback;
}

namespace {

std::vector<Artifact> exportCollectionImpl(const SessionSnapshot& snapshot, const SourceStore& sources,
                                           CollectionId collection, const ExportRequest& request,
                                           std::optional<AssetId> selectedSoundBank) {
  auto binding = bindCollection(snapshot, collection);
  if (!binding.collection) {
    return {Artifact{
        .filename = "export-error.txt",
        .mediaType = "text/plain",
        .diagnostics = std::move(binding.diagnostics),
    }};
  }
  CollectionWorkspace workspace{std::move(*binding.collection), std::move(binding.diagnostics)};
  const auto& bound = workspace.collection;
  // An empty request exports MIDI; other artifacts must be requested explicitly.
  static constexpr std::array defaultKinds{ExportKind::Midi};
  const std::span<const ExportKind> kinds = request.kinds.empty() ? std::span{defaultKinds} : std::span{request.kinds};
  const bool exportsMidi = std::ranges::find(kinds, ExportKind::Midi) != kinds.end();
  const bool exportsSynth = std::ranges::any_of(
      kinds, [](ExportKind kind) { return kind == ExportKind::SoundFont2 || kind == ExportKind::Dls; });
  const bool synthRequiresPerformance =
      (bound.hasSequence() && request.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants) ||
      request.exportOnlyUsedInstruments;
  const bool needsRendering = exportsMidi || (exportsSynth && (bound.hasSequence() || synthRequiresPerformance));

  if (needsRendering) {
    workspace.render(request.sequence, request.dynamicEnvelopes,
                     /*materializeSignedStereo=*/exportsMidi && exportsSynth);
  }
  const auto& rendering = workspace.rendering;
  const PerformanceSequence* preparedPerformance = workspace.performance();
  auto instruments = workspace.soundBankView();
  ModulationConversionPolicy synthConversion = request.modulationConversion;
  // Sequence-event simulation replaces native synth modulation only when a
  // companion MIDI artifact was requested and could actually be rendered.
  if (synthConversion == ModulationConversionPolicy::SequenceEventSimulation &&
      (!exportsMidi || !preparedPerformance)) {
    synthConversion = ModulationConversionPolicy::SynthModulators;
  }

  if (exportsMidi || exportsSynth) {
    workspace.prepareModulation(synthConversion, request.modulationScaling);
  }

  std::optional<MidiSequence> loweredMidi;
  if (exportsMidi && preparedPerformance) {
    loweredMidi = renderMidiSequence(*preparedPerformance, request.sequence.midi, request.modulationConversion,
                                     instruments, &rendering.modulation);
    applyMidiModulationScaling(*loweredMidi, workspace.modulationUsage, request.modulationScaling);
  }
  if (selectedSoundBank) {
    std::erase_if(instruments, [&](const SoundBankAsset* bank) { return bank->metadata.id != *selectedSoundBank; });
  }

  const auto writeSynth = [&](SynthExportFormat format) {
    if (synthRequiresPerformance && preparedPerformance == nullptr) {
      return synthArtifact(bound.baseName(), format, SynthExportResult{.diagnostics = rendering.diagnostics});
    }

    auto artifact = exportSynth(
        SynthExportInput{
            .name = bound.baseName(),
            .soundBanks = instruments,
            .samplePools = bound.samplePools(),
            .sequenceUsage = request.exportOnlyUsedInstruments ? preparedPerformance : nullptr,
            .filterSamplesToReferencedInstruments = selectedSoundBank.has_value(),
            .midiModulationUsage = &workspace.modulationUsage,
            .modulationScaling = request.modulationScaling,
            .modulationConversion = synthConversion,
            .sampleFiltering = request.sampleFiltering,
        },
        format, sources);
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
        artifacts.push_back(exportMidi(bound.baseName(), rendering, loweredMidi));
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
    artifact.diagnostics.insert(artifact.diagnostics.begin(), workspace.diagnostics.begin(),
                                workspace.diagnostics.end());
  }
  return artifacts;
}

}  // namespace

std::vector<Artifact> exportCollection(const SessionSnapshot& snapshot, const SourceStore& sources,
                                       CollectionId collection, const ExportRequest& request) {
  return exportCollectionImpl(snapshot, sources, collection, request, std::nullopt);
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
