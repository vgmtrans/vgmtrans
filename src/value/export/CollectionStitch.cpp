/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/CollectionStitch.h"

#include "value/export/DynamicEnvelope.h"
#include "value/export/ExportDiagnostics.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/SynthExportData.h"
#include "value/model/SessionSnapshot.h"
#include "value/scan/FormatRegistry.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <string_view>
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
  std::vector<InstrumentSetAsset> instruments;
  std::vector<const SampleCollectionAsset*> samples;
  PerformanceSequence performance;
  SequenceModulationProfile modulation;
  MidiSequence midi;
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

[[nodiscard]] const FormatModule* preparationModule(const Collection& collection, const SequenceProgramAsset& sequence,
                                                    const FormatRegistry& formats) {
  if (collection.origin == CollectionOrigin::UserCreated) {
    const auto* module = formats.findModule(sequence.metadata.format);
    if (module != nullptr && module->prepareCollection) {
      return module;
    }
  }
  const auto found = std::ranges::find_if(formats.modules(), [&](const FormatModule& module) {
    const std::string_view resolver =
        module.collectionResolverId.empty() ? std::string_view(module.name) : module.collectionResolverId;
    return module.prepareCollection && resolver == collection.key.resolver;
  });
  return found == formats.modules().end() ? nullptr : &*found;
}

[[nodiscard]] bool preparePart(StitchPart& part, const SessionSnapshot& snapshot, const SourceStore& sources,
                               const ExportRequest& request, const FormatRegistry& formats,
                               std::vector<Diagnostic>& diagnostics) {
  const Collection* collection = snapshot.collection(part.collection);
  if (collection == nullptr) {
    diagnostics.push_back(exportError("CollectionId was not found in the SessionSnapshot"));
    return false;
  }
  if (!collection->members.sequence) {
    diagnostics.push_back(exportError("A stitched collection does not contain a sequence"));
    return false;
  }
  const auto* sequence = snapshot.asset<SequenceProgramAsset>(*collection->members.sequence);
  if (sequence == nullptr) {
    diagnostics.push_back(exportError("A stitched collection's sequence asset was not found"));
    return false;
  }

  FinalizeCollectionPerformance finalize;
  std::optional<std::vector<InstrumentSetAsset>> replacements;
  if (const auto* module = preparationModule(*collection, *sequence, formats)) {
    try {
      auto prepared = module->prepareCollection(CollectionPrepareContext{
          .sources = sources,
          .snapshot = snapshot,
          .collection = *collection,
      });
      replacements = std::move(prepared.replacementInstrumentSets);
      finalize = std::move(prepared.finalizePerformance);
      append(diagnostics, prepared.diagnostics);
    } catch (const std::exception& error) {
      diagnostics.push_back(exportError(module->name + " collection preparation failed: " + error.what()));
      return false;
    }
  }

  if (replacements) {
    part.instruments = std::move(*replacements);
  } else {
    for (const AssetId id : collection->members.instrumentSets) {
      const auto* instruments = snapshot.asset<InstrumentSetAsset>(id);
      if (instruments == nullptr) {
        diagnostics.push_back(exportError("A stitched collection's instrument set was not found"));
        return false;
      }
      part.instruments.push_back(*instruments);
    }
  }
  for (const AssetId id : collection->members.sampleCollections) {
    const auto* samples = snapshot.asset<SampleCollectionAsset>(id);
    if (samples == nullptr) {
      diagnostics.push_back(exportError("A stitched collection's sample collection was not found"));
      return false;
    }
    part.samples.push_back(samples);
  }
  if (part.instruments.empty()) {
    diagnostics.push_back(exportError("A stitched collection does not contain instruments"));
    return false;
  }

  const auto* dialect = formats.findDialect(sequence->program.dialect.value);
  if (dialect == nullptr) {
    diagnostics.push_back(exportError("No sequence dialect registered for '" + sequence->program.dialect.value + "'"));
    return false;
  }
  part.performance = SequenceVm(SequenceVmOptions{
                                    .loopPolicy = request.sequence.loopPolicy,
                                    .sequenceLoops = request.sequence.sequenceLoops,
                                })
                         .render(sequence->program, *dialect);
  if (finalize) {
    try {
      finalize(part.performance);
    } catch (const std::exception& error) {
      diagnostics.push_back(exportError("Collection performance finalization failed: " + std::string(error.what())));
      return false;
    }
  }
  part.modulation = analyzeSequenceModulation(part.performance);
  if (request.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants) {
    auto materialized = materializeDynamicEnvelopes(part.performance, part.instruments);
    part.performance = std::move(materialized.performance);
    append(diagnostics, materialized.diagnostics);
  }
  if (request.modulationConversion == ModulationConversionPolicy::SynthModulators) {
    for (auto& instruments : part.instruments) {
      applySequenceModulation(instruments, part.modulation);
    }
  }

  std::vector<const InstrumentSetAsset*> instrumentView;
  instrumentView.reserve(part.instruments.size());
  for (const auto& instruments : part.instruments) {
    instrumentView.push_back(&instruments);
  }
  part.midi = renderMidiSequence(part.performance, request.sequence.midi, request.modulationConversion, instrumentView,
                                 &part.modulation);
  if (request.exportOnlyUsedInstruments) {
    const auto selected = selectSynthInstruments(instrumentView, &part.performance);
    const std::unordered_set<const Instrument*> used(selected.begin(), selected.end());
    for (auto& set : part.instruments) {
      std::vector<Instrument> retained;
      retained.reserve(set.instruments.size());
      for (auto& instrument : set.instruments) {
        if (used.contains(&instrument)) {
          retained.push_back(std::move(instrument));
        }
      }
      set.instruments = std::move(retained);
    }
    std::erase_if(part.instruments, [](const InstrumentSetAsset& set) { return set.instruments.empty(); });
  }
  return true;
}

[[nodiscard]] u32 logicalBank(const BankSelect& event, MidiBankSelectStyle style) {
  return style == MidiBankSelectStyle::MsbOnly ? event.bank >> 7 : event.bank;
}

[[nodiscard]] u16 midiBank(u32 bank, MidiBankSelectStyle style) {
  return static_cast<u16>(style == MidiBankSelectStyle::MsbOnly ? bank << 7 : bank);
}

[[nodiscard]] std::optional<u8> eventChannel(const MidiEvent& event) {
  return std::visit(
      [](const auto& typed) -> std::optional<u8> {
        if constexpr (requires { typed.channel; }) {
          return typed.channel;
        }
        return std::nullopt;
      },
      event);
}

void appendInitialChannelState(std::vector<MidiEvent>& events, u64 tick, u8 channel, u16 bank, bool writeBankLsb) {
  constexpr u16 defaultVolume = 100u << 7;
  constexpr u16 defaultExpression = 127u << 7;
  constexpr u8 defaultSoundController = 64;

  events.emplace_back(BankSelect{.tick = tick, .channel = channel, .bank = bank, .writeLsb = writeBankLsb});
  events.emplace_back(ProgramChange{.tick = tick, .channel = channel});
  events.emplace_back(Volume14{.tick = tick, .channel = channel, .value = defaultVolume});
  events.emplace_back(Pan{.tick = tick, .channel = channel});
  events.emplace_back(Expression14{.tick = tick, .channel = channel, .value = defaultExpression});
  events.emplace_back(Reverb{.tick = tick, .channel = channel});
  events.emplace_back(FineTune{.tick = tick, .channel = channel});
  events.emplace_back(CoarseTune{.tick = tick, .channel = channel});
  events.emplace_back(PitchBendRange{.tick = tick, .channel = channel});
  events.emplace_back(PitchBend{.tick = tick, .channel = channel});
  events.emplace_back(VibratoDepth{.tick = tick, .channel = channel});
  events.emplace_back(VibratoFrequency{.tick = tick, .channel = channel, .value = defaultSoundController});
  events.emplace_back(VibratoDelay{.tick = tick, .channel = channel, .ticks = defaultSoundController});
  events.emplace_back(TremoloDepth{.tick = tick, .channel = channel});
  events.emplace_back(TremoloFrequency{.tick = tick, .channel = channel, .value = defaultSoundController});
  events.emplace_back(TremoloDelay{.tick = tick, .channel = channel, .ticks = defaultSoundController});
  events.emplace_back(PortamentoEnable{.tick = tick, .channel = channel});
  events.emplace_back(PortamentoTime14{.tick = tick, .channel = channel});
  events.emplace_back(PortamentoControl{.tick = tick, .channel = channel});
  events.emplace_back(LegatoPedal{.tick = tick, .channel = channel});
}

[[nodiscard]] std::optional<u32> remappedBank(const StitchPart& part, u32 source) {
  const auto found = std::ranges::find(part.banks, source, &CollectionStitchBank::source);
  return found == part.banks.end() ? std::nullopt : std::optional{found->target};
}

[[nodiscard]] bool planBanks(std::vector<StitchPart>& parts, MidiBankSelectStyle style) {
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
        if (const auto* bank = std::get_if<BankSelect>(&event)) {
          sourceBanks.insert(logicalBank(*bank, style));
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

void remapPart(StitchPart& part, MidiBankSelectStyle style) {
  const std::optional<AssetId> fallbackSamples =
      part.samples.empty() ? std::nullopt : std::optional{part.samples.front()->metadata.id};
  for (auto& set : part.instruments) {
    for (auto& instrument : set.instruments) {
      auto address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
      address.bank = *remappedBank(part, address.bank);
      instrument.explicitAddress = address;
      for (auto& region : instrument.regions) {
        if (!region.sample.collection) {
          region.sample.collection = fallbackSamples.value_or(AssetId{});
        }
      }
    }
  }

  const u32 defaultBank = *remappedBank(part, 0);
  for (auto& track : part.performance.tracks) {
    for (auto& event : track.events) {
      if (auto* selection = std::get_if<InstrumentPerformanceEvent>(&event);
          selection && !selection->sourceInstrument) {
        if (const auto target = remappedBank(part, selection->bank)) {
          selection->bank = *target;
        }
      } else if (auto* note = std::get_if<NotePerformanceEvent>(&event); note && note->instrumentAddress) {
        if (const auto target = remappedBank(part, note->instrumentAddress->bank)) {
          note->instrumentAddress->bank = *target;
        }
      }
    }
    if (defaultBank != 0) {
      track.events.insert(track.events.begin(), InstrumentPerformanceEvent{
                                                    .bank = defaultBank,
                                                    .program = 0,
                                                    .forceBankSelect = true,
                                                });
    }
  }

  for (auto& track : part.midi.tracks) {
    for (auto& event : track.events) {
      if (auto* bank = std::get_if<BankSelect>(&event)) {
        bank->bank = midiBank(*remappedBank(part, logicalBank(*bank, style)), style);
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
  return std::visit(
      [](const auto& typed) {
        using Event = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Event, NoteDuration>) {
          return typed.tick > std::numeric_limits<u64>::max() - typed.duration ? std::numeric_limits<u64>::max()
                                                                               : typed.tick + typed.duration;
        }
        return typed.tick;
      },
      event);
}

[[nodiscard]] bool retime(MidiEvent& event, u32 sourcePpqn, u32 targetPpqn, u64 start) {
  return std::visit(
      [&](auto& typed) {
        const auto tick = scaled(typed.tick, sourcePpqn, targetPpqn);
        if (!tick || *tick > std::numeric_limits<u64>::max() - start) {
          return false;
        }
        if constexpr (std::is_same_v<std::decay_t<decltype(typed)>, NoteDuration>) {
          const auto end = scaled(eventEnd(event), sourcePpqn, targetPpqn);
          if (!end || *end < *tick || *end - *tick > std::numeric_limits<u32>::max()) {
            return false;
          }
          typed.duration = static_cast<u32>(*end - *tick);
        } else if constexpr (std::is_same_v<std::decay_t<decltype(typed)>, VibratoDelay> ||
                             std::is_same_v<std::decay_t<decltype(typed)>, TremoloDelay>) {
          const auto delay = scaled(typed.ticks, sourcePpqn, targetPpqn);
          if (!delay || *delay > std::numeric_limits<u32>::max()) {
            return false;
          }
          typed.ticks = static_cast<u32>(*delay);
        }
        typed.tick = *tick + start;
        return true;
      },
      event);
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
        std::vector<MidiEvent> initialState;
        initialState.reserve(channels.size() * 20);
        const u16 bank = midiBank(*remappedBank(part, 0), bankStyle);
        for (const u8 channel : channels) {
          appendInitialChannelState(initialState, cursor, channel, bank, bankStyle == MidiBankSelectStyle::MsbAndLsb);
        }
        track.events.insert(track.events.begin(), initialState.begin(), initialState.end());
      }
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
                                         std::span<const CollectionId> collections, const ExportRequest& request,
                                         const FormatRegistry& formats) {
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
    if (!preparePart(part, snapshot, sources, request, formats, result.midi.diagnostics)) {
      result.soundFont.diagnostics = result.midi.diagnostics;
      return result;
    }
    parts.push_back(std::move(part));
  }
  if (!planBanks(parts, request.sequence.midi.bankSelectStyle)) {
    fail(result, "Stitched collections require more than the 128 preset banks supported by SoundFont2");
    return result;
  }
  for (auto& part : parts) {
    remapPart(part, request.sequence.midi.bankSelectStyle);
  }

  auto composition = composeMidi(parts, request.sequence.midi.bankSelectStyle);
  if (!composition) {
    fail(result, "Stitched MIDI timeline exceeds the supported tick range");
    return result;
  }
  result.midi.bytes = encodeMidiFile(composition->midi);
  append(result.midi.diagnostics, composition->midi.diagnostics);

  std::vector<const InstrumentSetAsset*> instruments;
  std::vector<const SampleCollectionAsset*> samples;
  std::optional<PerformanceSequence> sequenceUsage;
  if (request.exportOnlyUsedInstruments) {
    sequenceUsage.emplace();
  }
  std::unordered_set<u32> includedCollections;
  std::unordered_set<u32> includedSamples;
  for (const auto& part : parts) {
    if (!includedCollections.insert(part.collection.value).second) {
      continue;
    }
    for (const auto& set : part.instruments) {
      instruments.push_back(&set);
    }
    if (sequenceUsage) {
      sequenceUsage->tracks.insert(sequenceUsage->tracks.end(), part.performance.tracks.begin(),
                                   part.performance.tracks.end());
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
          .instrumentSets = instruments,
          .sampleCollections = samples,
          .formats = &formats,
          .sequenceUsage = sequenceUsage ? &*sequenceUsage : nullptr,
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
