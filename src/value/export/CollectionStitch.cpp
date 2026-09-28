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
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

constexpr u32 kDefaultPpqn = 48;
constexpr u32 kMaximumPpqn = 1920;

struct StitchPart {
  std::shared_ptr<const PreparedCollection> prepared;
  u64 startTick = 0;
  InstrumentAddressPlan layout;
  MidiSequence midi;
  std::vector<CollectionStitchBank> banks;
};

void append(std::vector<Diagnostic>& destination, const std::vector<Diagnostic>& source) {
  destination.insert(destination.end(), source.begin(), source.end());
}

void fail(CollectionStitchResult& result, std::string message) {
  const Diagnostic diagnostic = exportError(std::move(message));
  result.midi.diagnostics.push_back(diagnostic);
  result.soundFont.diagnostics.push_back(diagnostic);
}

void mergeModulationUsage(MidiModulationUsage& destination, const MidiModulationUsage& source) {
  destination.vibratoDepth = std::max(destination.vibratoDepth, source.vibratoDepth);
  destination.vibratoRate = std::max(destination.vibratoRate, source.vibratoRate);
  destination.tremoloDepth = std::max(destination.tremoloDepth, source.tremoloDepth);
  destination.tremoloRate = std::max(destination.tremoloRate, source.tremoloRate);
}

[[nodiscard]] std::shared_ptr<const PreparedCollection> preparePart(
    CollectionId collection, const SessionSnapshot& snapshot, const ExportRequest& request,
    std::vector<Diagnostic>& diagnostics) {
  auto binding = bindCollection(snapshot, collection);
  append(diagnostics, binding.diagnostics);
  if (!binding.collection) {
    return nullptr;
  }
  const auto& bound = *binding.collection;
  if (!bound.hasSequence()) {
    diagnostics.push_back(exportError("A stitched collection does not contain a sequence"));
    return nullptr;
  }
  if (bound.soundBanks().empty()) {
    diagnostics.push_back(exportError("A stitched collection does not contain instruments"));
    return nullptr;
  }

  auto prepared = std::make_shared<PreparedCollection>(std::move(*binding.collection), CollectionPreparationOptions{
      .sequence = request.sequence,
      .variants = {.dynamicEnvelopes = request.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants,
                   .signedStereo = true},
      .modulationConversion = request.modulationConversion,
      .modulationScaling = request.modulationScaling,
  });
  if (!prepared->performance()) {
    append(diagnostics, prepared->rendering.diagnostics);
    return nullptr;
  }
  return prepared;
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

[[nodiscard]] u32 normalizedPpqn(u32 ppqn) {
  return ppqn == 0 ? kDefaultPpqn : ppqn;
}

[[nodiscard]] u32 commonPpqn(const std::vector<StitchPart>& parts) {
  u32 common = 0;
  for (const auto& part : parts) {
    const u32 ppqn = normalizedPpqn(part.midi.timebase.midiDivision());
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
  return addTicks(event.tick, note != nullptr ? note->duration : 0);
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

[[nodiscard]] std::optional<MidiSequence> composeMidi(std::vector<StitchPart>& parts,
                                                      MidiBankSelectStyle bankStyle) {
  MidiSequence midi;
  midi.timebase.ppqn = commonPpqn(parts);
  u64 cursor = 0;
  for (size_t partIndex = 0; partIndex < parts.size(); ++partIndex) {
    auto& part = parts[partIndex];
    part.startTick = cursor;
    const u32 sourcePpqn = normalizedPpqn(part.midi.timebase.ppqn);
    u64 end = 0;
    for (auto& track : part.midi.tracks) {
      end = std::max(end, track.endTick);
      for (auto& event : track.events) {
        end = std::max(end, eventEnd(event));
        if (!retime(event, sourcePpqn, midi.timebase.ppqn, cursor)) {
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
      const auto trackEnd = scaled(track.endTick, sourcePpqn, midi.timebase.ppqn);
      if (!trackEnd || *trackEnd > std::numeric_limits<u64>::max() - cursor) {
        return std::nullopt;
      }
      track.endTick = *trackEnd + cursor;
      midi.tracks.push_back(std::move(track));
    }
    append(midi.diagnostics, part.midi.diagnostics);
    const auto duration = scaled(end, sourcePpqn, midi.timebase.ppqn);
    if (!duration || *duration > std::numeric_limits<u64>::max() - cursor) {
      return std::nullopt;
    }
    cursor += *duration;
  }
  return midi;
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
  u32 nextBank = 0;
  for (const CollectionId collection : collections) {
    if (const auto previous = std::ranges::find(parts, collection, [](const StitchPart& part) {
          return part.prepared->id;
        }); previous != parts.end()) {
      parts.push_back(*previous);
      continue;
    }
    auto prepared = preparePart(collection, snapshot, request, result.midi.diagnostics);
    if (!prepared) {
      result.soundFont.diagnostics = result.midi.diagnostics;
      return result;
    }
    StitchPart part{.prepared = std::move(prepared)};
    part.layout = planInstrumentAddresses(*part.prepared->performance(), request.exportOnlyUsedInstruments, nextBank);
    append(result.midi.diagnostics, part.layout.diagnostics);
    if (!part.layout.valid) {
      result.soundFont.diagnostics = result.midi.diagnostics;
      return result;
    }
    nextBank = part.layout.nextBank();
    for (const auto& [source, target] : part.layout.banks) part.banks.push_back({source, target});
    parts.push_back(std::move(part));
  }

  MidiModulationUsage modulationUsage;
  for (const auto& part : parts) {
    mergeModulationUsage(modulationUsage, part.prepared->modulationUsage);
  }
  for (auto& part : parts) {
    part.midi = renderMidiSequence(*part.prepared->performance(), part.layout, request.sequence.midi,
                                  request.modulationConversion, &part.prepared->rendering.modulation);
    applyMidiModulationScaling(part.midi, modulationUsage, request.modulationScaling);
  }

  auto midi = composeMidi(parts, request.sequence.midi.bankSelectStyle);
  if (!midi) {
    fail(result, "Stitched MIDI timeline exceeds the supported tick range");
    return result;
  }
  result.midi.bytes = encodeMidiFile(*midi);
  append(result.midi.diagnostics, midi->diagnostics);

  std::vector<SynthBankSelection> synthBanks;
  std::vector<const SamplePoolAsset*> samples;
  std::unordered_set<u32> includedCollections;
  std::unordered_set<u32> includedSamples;
  for (const auto& part : parts) {
    if (!includedCollections.insert(part.prepared->id.value).second) {
      continue;
    }
    auto selected = selectSynthBanks(*part.prepared->performance(), part.layout, request.exportOnlyUsedInstruments);
    synthBanks.insert(synthBanks.end(), std::make_move_iterator(selected.begin()), std::make_move_iterator(selected.end()));
    for (const auto* collection : part.prepared->samplePools) {
      if (includedSamples.insert(collection->metadata.id.value).second) {
        samples.push_back(collection);
      }
    }
  }
  auto soundFont = buildSoundFont2(
      SynthExportInput{
          .name = "Stitched Collections",
          .soundBanks = std::move(synthBanks),
          .samplePools = samples,
          .filterSamplesToReferencedInstruments = request.exportOnlyUsedInstruments,
          .midiModulationUsage = &modulationUsage,
          .modulationScaling = request.modulationScaling,
          .modulationConversion = request.modulationConversion,
          .sampleFiltering = request.sampleFiltering,
      },
      sources);
  result.soundFont.bytes = std::move(soundFont.bytes);
  result.soundFont.diagnostics = result.midi.diagnostics;
  append(result.soundFont.diagnostics, soundFont.diagnostics);

  result.parts.reserve(parts.size());
  for (auto& part : parts) {
    result.parts.push_back(CollectionStitchPart{
        .collection = part.prepared->id,
        .startTick = part.startTick,
        .banks = std::move(part.banks),
    });
  }
  return result;
}

}  // namespace vgmtrans::core
