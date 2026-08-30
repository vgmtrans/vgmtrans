/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/CollectionStitch.h"

#include "value/export/CollectionBinding.h"
#include "value/export/ExportDiagnostics.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/ModulationAnalysis.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/export/synth/SynthExportData.h"
#include "value/model/SessionSnapshot.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

constexpr u32 kDefaultPpqn = 48;
constexpr u32 kMaximumPpqn = 1920;

struct StitchPart {
  CollectionId collection;
  std::vector<SoundBankAsset> instruments;
  std::vector<const SamplePoolAsset*> samples;
  MidiSequence midi;
  std::optional<MidiModulationUsage> modulationUsage;
  std::vector<CollectionStitchBank> banks;
};

struct ComposedMidi {
  MidiSequence midi;
  std::vector<u64> starts;
};

void append(std::vector<Diagnostic>& destination, const std::vector<Diagnostic>& source) {
  destination.insert(destination.end(), source.begin(), source.end());
}

void fail(CollectionStitchResult& result, std::string message) {
  const Diagnostic diagnostic = exportError(std::move(message));
  result.midi.diagnostics.push_back(diagnostic);
  result.soundFont.diagnostics.push_back(diagnostic);
}

void mergeMaximum(std::optional<MidiModulationMaximum>& destination,
                  const std::optional<MidiModulationMaximum>& source) {
  if (!source) {
    return;
  }
  if (!destination) {
    destination = source;
    return;
  }
  destination->controllerValue = std::max(destination->controllerValue, source->controllerValue);
  destination->normalized = std::max(destination->normalized, source->normalized);
}

void mergeModulationUsage(MidiModulationUsage& destination, const MidiModulationUsage& source) {
  mergeMaximum(destination.vibratoDepth, source.vibratoDepth);
  mergeMaximum(destination.vibratoRate, source.vibratoRate);
  mergeMaximum(destination.tremoloDepth, source.tremoloDepth);
  mergeMaximum(destination.tremoloRate, source.tremoloRate);
}

[[nodiscard]] bool preparePart(StitchPart& part, const SessionSnapshot& snapshot, const ExportRequest& request,
                               std::vector<Diagnostic>& diagnostics) {
  auto binding = bindCollection(snapshot, part.collection);
  if (!binding.collection) {
    append(diagnostics, binding.diagnostics);
    return false;
  }
  CollectionWorkspace workspace{std::move(*binding.collection), std::move(binding.diagnostics)};
  const auto& bound = workspace.collection;
  if (!bound.hasSequence()) {
    append(diagnostics, workspace.diagnostics);
    diagnostics.push_back(exportError("A stitched collection does not contain a sequence"));
    return false;
  }
  if (bound.soundBanks().empty()) {
    append(diagnostics, workspace.diagnostics);
    diagnostics.push_back(exportError("A stitched collection does not contain instruments"));
    return false;
  }

  workspace.render(request.sequence, request.dynamicEnvelopes, /*materializeSignedStereo=*/true);
  if (!workspace.performance()) {
    append(diagnostics, workspace.diagnostics);
    append(diagnostics, workspace.rendering.diagnostics);
    return false;
  }

  workspace.prepareSynth(request.modulationConversion, request.modulationScaling);
  const PerformanceSequence* performance = workspace.performance();
  const auto instruments = workspace.soundBankView();
  part.midi = renderMidiSequence(*performance, request.sequence.midi, request.modulationConversion, instruments,
                                 &workspace.rendering.modulation);
  part.modulationUsage = std::move(workspace.modulationUsage);
  auto& soundBanks = workspace.soundBanks();
  if (request.exportOnlyUsedInstruments) {
    const auto selected = selectSynthInstruments(instruments, performance);
    const std::unordered_set<const Instrument*> used(selected.begin(), selected.end());
    for (auto& set : soundBanks) {
      std::vector<Instrument> retained;
      retained.reserve(set.instruments.size());
      for (auto& instrument : set.instruments) {
        if (used.contains(&instrument)) {
          retained.push_back(std::move(instrument));
        }
      }
      set.instruments = std::move(retained);
    }
    std::erase_if(soundBanks, [](const SoundBankAsset& set) { return set.instruments.empty(); });
  }

  part.instruments = std::move(soundBanks);
  part.samples = bound.samplePools();
  append(diagnostics, workspace.diagnostics);
  return true;
}

[[nodiscard]] std::optional<u8> eventChannel(const MidiEvent& event) {
  return std::visit(
      [](const auto& typed) -> std::optional<u8> {
        if constexpr (requires { typed.channel; }) {
          return typed.channel;
        }
        return std::nullopt;
      },
      event.payload);
}

void appendInitialChannelState(MidiTrack& track, u64 tick, u8 channel, u16 bank, bool writeBankLsb) {
  constexpr u16 defaultVolume = 100u << 7;
  constexpr u16 defaultExpression = 127u << 7;
  constexpr u8 defaultSoundController = 64;

  track.events.push_back(midi::bankSelect(tick, channel, bank, writeBankLsb));
  track.events.push_back(midi::programChange(tick, channel, 0));
  midi::appendController14(track, tick, channel, MidiController::ChannelVolume, defaultVolume);
  track.events.push_back(midi::controller(tick, channel, MidiController::Pan, 64));
  midi::appendController14(track, tick, channel, MidiController::Expression, defaultExpression);
  track.events.push_back(midi::controller(tick, channel, MidiController::Reverb, 0));
  midi::appendRpn(track, tick, channel, 0, 1, 8192, 8);
  midi::appendRpn(track, tick, channel, 0, 2, 8192, 8);
  midi::appendRpn(track, tick, channel, 0, 0, 2u << 7);
  track.events.push_back(midi::pitchBend(tick, channel, 0));
  track.events.push_back(midi::controller(tick, channel, MidiController::Modulation, 0));
  track.events.push_back(midi::controller(tick, channel, MidiController::VibratoRate, defaultSoundController));
  track.events.push_back(midi::controller(tick, channel, MidiController::VibratoDelay, defaultSoundController));
  track.events.push_back(midi::controller(tick, channel, MidiController::TremoloDepth, 0));
  track.events.push_back(midi::controller(tick, channel, MidiController::TremoloRate, defaultSoundController));
  track.events.push_back(midi::controller(tick, channel, MidiController::TremoloDelay, defaultSoundController));
  track.events.push_back(midi::controller(tick, channel, MidiController::Portamento, 0));
  midi::appendController14(track, tick, channel, MidiController::PortamentoTime, 0, true);
  track.events.push_back(midi::controller(tick, channel, MidiController::PortamentoControl, 0));
  track.events.push_back(midi::controller(tick, channel, MidiController::Legato, 0));
}

[[nodiscard]] std::optional<u32> remappedBank(const StitchPart& part, u32 source) {
  const auto found = std::ranges::find(part.banks, source, &CollectionStitchBank::source);
  return found == part.banks.end() ? std::nullopt : std::optional{found->target};
}

[[nodiscard]] bool planBanks(std::vector<StitchPart>& parts) {
  u32 nextBank = 0;
  std::unordered_map<u32, std::vector<CollectionStitchBank>> planned;
  for (auto& part : parts) {
    if (const auto previous = planned.find(part.collection.value); previous != planned.end()) {
      part.banks = previous->second;
      continue;
    }

    std::set<u32> sourceBanks{0};
    for (const auto& set : part.instruments) {
      for (const auto& instrument : set.instruments) {
        sourceBanks.insert(resolveInstrumentAddress(instrument.explicitAddress, instrument.identity).bank);
      }
    }
    for (const auto& track : part.midi.tracks) {
      for (const auto& event : track.events) {
        if (const auto* bank = std::get_if<BankSelect>(&event.payload)) {
          sourceBanks.insert(bank->bank);
        }
      }
    }
    if (sourceBanks.size() > 128 - nextBank) {
      return false;
    }
    for (const u32 source : sourceBanks) {
      part.banks.push_back(CollectionStitchBank{.source = source, .target = nextBank++});
    }
    planned.emplace(part.collection.value, part.banks);
  }
  return true;
}

void remapPart(StitchPart& part) {
  for (auto& set : part.instruments) {
    for (auto& instrument : set.instruments) {
      auto address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
      address.bank = *remappedBank(part, address.bank);
      instrument.explicitAddress = address;
    }
  }

  for (auto& track : part.midi.tracks) {
    for (auto& event : track.events) {
      if (auto* bank = std::get_if<BankSelect>(&event.payload)) {
        bank->bank = static_cast<u16>(*remappedBank(part, bank->bank));
      }
    }
  }
}

[[nodiscard]] u32 normalizedPpqn(u32 ppqn) {
  return ppqn == 0 ? kDefaultPpqn : ppqn;
}

[[nodiscard]] u32 commonPpqn(const std::vector<StitchPart>& parts) {
  u32 common = 0;
  for (const auto& part : parts) {
    const u32 ppqn = normalizedPpqn(part.midi.timebase.ppqn);
    if (common == 0) {
      common = ppqn;
      continue;
    }
    const u64 multiple = std::lcm<u64>(common, ppqn);
    common = multiple <= kMaximumPpqn ? static_cast<u32>(multiple) : std::max(common, ppqn);
  }
  return normalizedPpqn(common);
}

[[nodiscard]] std::optional<u64> scaled(u64 tick, u32 sourcePpqn, u32 targetPpqn) {
  const u64 whole = tick / sourcePpqn;
  const u64 fraction = ((tick % sourcePpqn) * targetPpqn + sourcePpqn / 2) / sourcePpqn;
  if (whole > (std::numeric_limits<u64>::max() - fraction) / targetPpqn) {
    return std::nullopt;
  }
  return whole * targetPpqn + fraction;
}

[[nodiscard]] u64 eventEnd(const MidiEvent& event) {
  const auto* note = std::get_if<NoteDuration>(&event.payload);
  return note != nullptr && event.tick > std::numeric_limits<u64>::max() - note->duration
             ? std::numeric_limits<u64>::max()
             : event.tick + (note != nullptr ? note->duration : 0);
}

[[nodiscard]] bool retime(MidiEvent& event, u32 sourcePpqn, u32 targetPpqn, u64 start) {
  const auto tick = scaled(event.tick, sourcePpqn, targetPpqn);
  if (!tick || *tick > std::numeric_limits<u64>::max() - start) {
    return false;
  }
  if (auto* note = std::get_if<NoteDuration>(&event.payload)) {
    const auto end = scaled(eventEnd(event), sourcePpqn, targetPpqn);
    if (!end || *end < *tick || *end - *tick > std::numeric_limits<u32>::max()) {
      return false;
    }
    note->duration = static_cast<u32>(*end - *tick);
  }
  event.tick = *tick + start;
  return true;
}

[[nodiscard]] std::optional<ComposedMidi> composeMidi(const std::vector<StitchPart>& parts,
                                                      MidiBankSelectStyle bankStyle) {
  ComposedMidi composition;
  composition.midi.timebase.ppqn = commonPpqn(parts);
  u64 cursor = 0;
  for (size_t partIndex = 0; partIndex < parts.size(); ++partIndex) {
    const auto& part = parts[partIndex];
    composition.starts.push_back(cursor);
    const u32 sourcePpqn = normalizedPpqn(part.midi.timebase.ppqn);
    u64 end = 0;
    for (const auto& sourceTrack : part.midi.tracks) {
      auto track = sourceTrack;
      end = std::max(end, sourceTrack.endTick);
      for (auto& event : track.events) {
        end = std::max(end, eventEnd(event));
        if (!retime(event, sourcePpqn, composition.midi.timebase.ppqn, cursor)) {
          return std::nullopt;
        }
      }
      if (partIndex != 0) {
        // Each source MIDI assumes fresh channel state. Add the boundary after
        // retiming so controller values that happen to use ticks stay unscaled.
        std::set<u8> channels;
        for (const auto& event : track.events) {
          if (const auto channel = eventChannel(event)) {
            channels.insert(*channel);
          }
        }
        MidiTrack initialState;
        initialState.events.reserve(channels.size() * 32);
        const u16 bank = static_cast<u16>(*remappedBank(part, 0));
        for (const u8 channel : channels) {
          appendInitialChannelState(initialState, cursor, channel, bank, bankStyle == MidiBankSelectStyle::MsbAndLsb);
        }
        track.events.insert(track.events.begin(), initialState.events.begin(), initialState.events.end());
      }
      const auto trackEnd = scaled(sourceTrack.endTick, sourcePpqn, composition.midi.timebase.ppqn);
      if (!trackEnd || *trackEnd > std::numeric_limits<u64>::max() - cursor) {
        return std::nullopt;
      }
      track.endTick = *trackEnd + cursor;
      composition.midi.tracks.push_back(std::move(track));
    }
    append(composition.midi.diagnostics, part.midi.diagnostics);
    const auto duration = scaled(end, sourcePpqn, composition.midi.timebase.ppqn);
    if (!duration || *duration > std::numeric_limits<u64>::max() - cursor) {
      return std::nullopt;
    }
    cursor += *duration;
  }
  return composition;
}

}  // namespace

CollectionStitchResult stitchCollections(const SessionSnapshot& snapshot, const SourceStore& sources,
                                         std::span<const CollectionId> collections, const ExportRequest& request) {
  CollectionStitchResult result{
      .midi = Artifact{.filename = "stitched-collections.mid", .mediaType = "audio/midi"},
      .soundFont = Artifact{.filename = "stitched-collections.sf2", .mediaType = "audio/soundfont"},
  };
  if (collections.size() < 2) {
    fail(result, "At least two collections are required for stitched export");
    return result;
  }

  std::vector<StitchPart> parts;
  parts.reserve(collections.size());
  for (const CollectionId collection : collections) {
    StitchPart part{.collection = collection};
    if (!preparePart(part, snapshot, request, result.midi.diagnostics)) {
      result.soundFont.diagnostics = result.midi.diagnostics;
      return result;
    }
    parts.push_back(std::move(part));
  }

  std::optional<MidiModulationUsage> modulationUsage;
  for (const auto& part : parts) {
    if (part.modulationUsage) {
      if (!modulationUsage) {
        modulationUsage.emplace();
      }
      mergeModulationUsage(*modulationUsage, *part.modulationUsage);
    }
  }
  if (modulationUsage) {
    for (auto& part : parts) {
      applyMidiModulationScaling(part.midi, *modulationUsage, request.modulationScaling);
    }
  }

  if (!planBanks(parts)) {
    fail(result, "Stitched collections require more than the 128 preset banks supported by SoundFont2");
    return result;
  }
  for (auto& part : parts) {
    remapPart(part);
  }

  auto composition = composeMidi(parts, request.sequence.midi.bankSelectStyle);
  if (!composition) {
    fail(result, "Stitched MIDI timeline exceeds the supported tick range");
    return result;
  }
  result.midi.bytes = encodeMidiFile(composition->midi);
  append(result.midi.diagnostics, composition->midi.diagnostics);

  std::vector<const SoundBankAsset*> instruments;
  std::vector<const SamplePoolAsset*> samples;
  std::unordered_set<u32> includedCollections;
  std::unordered_set<u32> includedSamples;
  for (const auto& part : parts) {
    if (!includedCollections.insert(part.collection.value).second) {
      continue;
    }
    for (const auto& set : part.instruments) {
      instruments.push_back(&set);
    }
    for (const auto* collection : part.samples) {
      if (includedSamples.insert(collection->metadata.id.value).second) {
        samples.push_back(collection);
      }
    }
  }
  auto soundFont = buildSoundFont2(
      SynthExportInput{
          .name = "Stitched Collections",
          .soundBanks = instruments,
          .samplePools = samples,
          .filterSamplesToReferencedInstruments = request.exportOnlyUsedInstruments,
          .midiModulationUsage = modulationUsage ? &*modulationUsage : nullptr,
          .modulationScaling = request.modulationScaling,
          .modulationConversion = request.modulationConversion,
          .sampleFiltering = request.sampleFiltering,
      },
      sources);
  result.soundFont.bytes = std::move(soundFont.bytes);
  result.soundFont.diagnostics = result.midi.diagnostics;
  append(result.soundFont.diagnostics, soundFont.diagnostics);

  result.parts.reserve(parts.size());
  for (size_t index = 0; index < parts.size(); ++index) {
    result.parts.push_back(CollectionStitchPart{
        .collection = parts[index].collection,
        .startTick = composition->starts[index],
        .banks = std::move(parts[index].banks),
    });
  }
  return result;
}

}  // namespace vgmtrans::core
