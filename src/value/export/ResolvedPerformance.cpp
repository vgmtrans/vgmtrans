/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "value/export/ResolvedPerformance.h"
#include "value/export/synth/SynthExportData.h"
#include "value/synth/SynthMath.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace vgmtrans::core {

namespace {

struct EnvelopeOverride {
  Envelope values;
  EnvelopeFields fields = EnvelopeFields::None;
};

struct LaneState {
  EnvelopeOverride envelope;
  double pan = 0.5;
  u64 voiceEnd = 0;
};

void applyEnvelopeUpdate(EnvelopeOverride& state, const EnvelopeUpdate& update) {
  if (!update.values) {
    state.fields = static_cast<EnvelopeFields>(static_cast<u8>(state.fields) & ~static_cast<u8>(update.fields));
    return;
  }
  for (const auto& [field, member] : detail::envelopeFields) {
    if (hasEnvelopeField(update.fields, field)) {
      state.values.*member = (*update.values).*member;
    }
  }
  state.fields |= update.fields;
}

[[nodiscard]] Envelope applyEnvelopeOverride(Envelope envelope, const EnvelopeOverride& state) {
  for (const auto& [field, member] : detail::envelopeFields) {
    if (hasEnvelopeField(state.fields, field)) {
      envelope.*member = state.values.*member;
    }
  }
  return envelope;
}

[[nodiscard]] bool validEnvelopeUpdate(const EnvelopeUpdate& update) {
  constexpr u8 knownFields = static_cast<u8>(EnvelopeFields::All);
  if ((static_cast<u8>(update.fields) & ~knownFields) != 0 || !update.values) {
    return (static_cast<u8>(update.fields) & ~knownFields) == 0;
  }
  for (const auto& [field, member] : detail::envelopeFields) {
    if (!hasEnvelopeField(update.fields, field)) {
      continue;
    }
    const auto value = update.values.value().*member;
    if (field == EnvelopeFields::Sustain) {
      if (value && (!std::isfinite(*value) || *value < 0.0 || *value > 1.0)) {
        return false;
      }
    } else if (value && (std::isnan(*value) || *value < 0.0)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Diagnostic variantWarning(std::string code, std::string message, const PerformanceEventHeader& header) {
  return Diagnostic{
      .severity = Severity::Warning,
      .code = std::move(code),
      .message = std::move(message),
      .annotation = header.sourceAnnotation.valid() ? std::optional{header.sourceAnnotation} : std::nullopt,
  };
}

[[nodiscard]] Diagnostic instrumentNotFoundWarning(bool dynamicEnvelopeOnly, const PerformanceEventHeader& header) {
  if (dynamicEnvelopeOnly) {
    return variantWarning("dynamic-envelope-instrument-not-found",
                          "Could not resolve the selected instrument for a dynamic envelope variant", header);
  }
  return variantWarning("instrument-variant-instrument-not-found",
                        "Could not resolve the selected instrument for an export-only variant", header);
}

[[nodiscard]] Diagnostic noRegionsWarning(bool dynamicEnvelopeOnly, const PerformanceEventHeader& header) {
  if (dynamicEnvelopeOnly) {
    return variantWarning("dynamic-envelope-no-regions",
                          "Could not apply a dynamic envelope to an instrument without sampled regions", header);
  }
  return variantWarning("instrument-variant-no-regions",
                        "Could not materialize an instrument variant without sampled regions", header);
}

enum class WarningSourceKind { Annotation, Command, Event };
using WarningSourceKey = std::tuple<WarningSourceKind, u32, u64>;

[[nodiscard]] WarningSourceKey warningSourceKey(const PerformanceEventHeader& header) noexcept {
  if (header.sourceAnnotation.valid()) {
    return {WarningSourceKind::Annotation, header.sourceAnnotation.value, 0};
  }
  if (header.sourceCommand.valid()) {
    return {WarningSourceKind::Command, header.sourceCommand.track.value, header.sourceCommand.id.value};
  }
  return {WarningSourceKind::Event, header.track.value, header.sequence};
}

struct VariantRecord {
  InstrumentHandle base;
  InstrumentHandle variant;
};

[[nodiscard]] bool sameVariantRegion(const Region& left, const Region& right) {
  return left.envelope == right.envelope && left.pan == right.pan && left.attenuationDb == right.attenuationDb &&
         left.invertSamplePhase == right.invertSamplePhase;
}

[[nodiscard]] const VariantRecord* findVariant(std::span<const VariantRecord> variants, InstrumentHandle base,
                                               std::span<const Region> regions, std::span<const SoundBankAsset> soundBanks) {
  const auto found = std::ranges::find_if(variants, [&](const VariantRecord& variant) {
    return variant.base == base && std::ranges::equal(soundBanks[variant.variant.bank].instruments[variant.variant.instrument].regions, regions,
                                                     sameVariantRegion);
  });
  return found == variants.end() ? nullptr : &*found;
}

[[nodiscard]] bool requiresSignedStereoVariants(const PerformanceTrack& track) {
  return std::ranges::any_of(track.events, [](const PerformanceEvent& event) {
    const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event);
    return balance != nullptr && (balance->leftGain < 0.0 || balance->rightGain < 0.0);
  });
}

void appendStereoLayers(std::vector<Region>& layers, const Region& source, double leftGain, double rightGain,
                        double pan) {
  constexpr double piOverTwo = 1.57079632679489661923;
  // SF2 combines channel and region pan additively. Resolve that composition
  // at attack time, then express its signed left/right output as two layers.
  const double position = std::clamp(source.pan + pan - 0.5, 0.0, 1.0);
  const std::array gains{
      leftGain * std::cos(position * piOverTwo),
      rightGain * std::sin(position * piOverTwo),
  };
  for (size_t channel = 0; channel < gains.size(); ++channel) {
    if (std::abs(gains[channel]) < 0.000000001) {
      continue;
    }
    Region layer = source;
    layer.pan = static_cast<double>(channel);
    layer.attenuationDb += linearAmplitudeToAttenuationDb(std::abs(gains[channel]));
    if (gains[channel] < 0.0) {
      layer.invertSamplePhase = !layer.invertSamplePhase;
    }
    layers.push_back(std::move(layer));
  }
}

bool matchesInstrumentSelection(const Instrument& instrument, const InstrumentSelection& selection) {
  if (const auto* identity = std::get_if<InstrumentIdentity>(&selection)) {
    return instrument.identity && *instrument.identity == *identity;
  }
  return instrument.explicitAddress == std::get<InstrumentAddress>(selection);
}
}  // namespace

ResolvedPerformance::ResolvedPerformance(PerformanceSequence performance, std::vector<SoundBankAsset> soundBanks,
                                         InstrumentSelection initialInstrument)
    : performance_(std::move(performance)),
      soundBanks_(std::make_shared<const std::vector<SoundBankAsset>>(std::move(soundBanks))),
      initialInstrument_(std::move(initialInstrument)) {}

std::vector<const SoundBankAsset*> ResolvedPerformance::soundBankView() const {
  std::vector<const SoundBankAsset*> view;
  for (const auto& bank : *soundBanks_) view.push_back(&bank);
  return view;
}

const Instrument* ResolvedPerformance::instrument(const InstrumentSelection& selection) const {
  if (const auto* handle = std::get_if<InstrumentHandle>(&selection)) {
    return &soundBanks_->at(handle->bank).instruments.at(handle->instrument);
  }
  if (std::holds_alternative<InstrumentIdentity>(selection)) {
    throw std::logic_error("Unresolved native instrument reached output conversion");
  }
  return nullptr;
}

std::set<InstrumentHandle> ResolvedPerformance::usedInstruments() const {
  std::set<InstrumentHandle> used;
  for (const auto& track : performance_.tracks) {
    for (const auto& event : track.events) {
      if (const auto* note = std::get_if<NotePerformanceEvent>(&event); note && note->instrument) {
        if (const auto* handle = std::get_if<InstrumentHandle>(&*note->instrument)) {
          used.insert(*handle);
        }
      }
    }
  }
  return used;
}

ResolvedPerformance preparePerformance(PerformanceSequence performance, std::vector<SoundBankAsset> soundBanks,
                                         InstrumentVariantOptions options) {
  auto& diagnostics = performance.diagnostics;
  struct CachedSelection {
    InstrumentSelection resolved;
    bool fallback = false;
    bool conflict = false;
    bool reported = false;
  };
  std::map<InstrumentSelection, CachedSelection> cache;
  // Freeze source-address preferences before adapting any notes. Generated
  // variants have neither identity nor address and cannot satisfy a later lookup.
  for (auto& bank : soundBanks) {
    for (auto& instrument : bank.instruments) {
      instrument.explicitAddress = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
    }
  }
  const auto resolve = [&](const InstrumentSelection& source,
                           const PerformanceEventHeader* header) -> InstrumentSelection {
    auto [found, inserted] = cache.try_emplace(source);
    if (inserted) {
      if (std::holds_alternative<InstrumentHandle>(source)) {
        throw std::logic_error("Resolve source selections only once, before output conversion");
      }
      std::optional<InstrumentHandle> first;
      size_t count = 0;
      const auto matches = [&](const InstrumentSelection& selection) {
        for (u32 bank = 0; bank < soundBanks.size(); ++bank) {
          for (u32 index = 0; index < soundBanks[bank].instruments.size(); ++index) {
            if (matchesInstrumentSelection(soundBanks[bank].instruments[index], selection)) {
              if (!first) first = InstrumentHandle{bank, index};
              ++count;
            }
          }
        }
      };
      matches(source);
      bool fallback = false;
      if (!first && std::holds_alternative<InstrumentIdentity>(source)) {
        matches(resolveInstrumentAddress(source));
        fallback = first.has_value();
      }
      found->second = {first ? InstrumentSelection{*first} : InstrumentSelection{resolveInstrumentAddress(source)},
                       fallback, count > 1};
    }
    if (header && !found->second.reported) {
      const auto warn = [&](std::string code, std::string message) {
        diagnostics.push_back(Diagnostic{
            .severity = Severity::Warning, .code = std::move(code), .message = std::move(message),
            .annotation = header->sourceAnnotation.valid() ? std::optional{header->sourceAnnotation} : std::nullopt});
      };
      if (found->second.fallback) warn("instrument-selection-fallback",
                               "Native instrument was not found; using its numeric preset address");
      if (found->second.conflict) warn("instrument-selection-conflict",
                               "Several instruments match; using the first definition in selected bank order");
      found->second.reported = true;
    }
    return found->second.resolved;
  };
  const auto initialInstrument = resolve(InstrumentAddress{}, nullptr);
  std::set<u32> sampledBanks;
  std::vector<VariantRecord> variants;
  std::set<WarningSourceKey> activeEnvelopeWarnings;
  std::set<WarningSourceKey> activeStereoWarnings;
  std::set<InstrumentHandle> regionlessInstrumentWarnings;

  for (auto& track : performance.tracks) {
    std::ranges::stable_sort(track.events, {}, [](const PerformanceEvent& event) {
      return performanceEventHeader(event).order();
    });
    const auto continuedNotes = performanceNotePredecessors(track);
    InstrumentSelection selected = InstrumentAddress{};
    // Once a track uses signed channel gain, all of its pan must be baked into
    // variants so ordinary MIDI pan does not also affect the layered output.
    const bool materializeStereo = options.signedStereo && requiresSignedStereoVariants(track);
    std::unordered_map<PerformanceNoteId, InstrumentSelection> attacks;
    std::optional<InstrumentSelection> previous;
    std::unordered_map<PerformanceLaneId, LaneState> lanes;
    double leftGain = 1.0;
    double rightGain = 1.0;
    bool warnedMissingInstrument = false;
    const auto warnActiveStereoChange = [&](bool changed, bool active, const PerformanceEventHeader& header) {
      if (changed && active && activeStereoWarnings.insert(warningSourceKey(header)).second) {
        diagnostics.push_back(variantWarning(
            "signed-stereo-active-voice",
            "A phase or pan change occurred during a sounding note; stereo instrument variants apply it to "
            "future note attacks only",
            header));
      }
    };

    for (auto& event : track.events) {
      if (auto* selection = std::get_if<InstrumentPerformanceEvent>(&event)) {
        selected = selection->instrument;
        selection->forceBankSelect |= std::holds_alternative<InstrumentIdentity>(selected);
        selection->instrument = resolve(selected, &selection->header);
        if (selection->envelopeMode == InstrumentEnvelopeMode::UseInstrumentEnvelope) {
          for (auto& [_, lane] : lanes) {
            lane.envelope = {};
          }
        }
        continue;
      }

      if (const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event);
          envelope != nullptr && options.dynamicEnvelopes) {
        if (!validEnvelopeUpdate(envelope->update)) {
          diagnostics.push_back(variantWarning("dynamic-envelope-invalid",
                                                "Ignored an invalid dynamic envelope update", envelope->header));
          continue;
        }

        const bool affectsActive = envelope->scope != VoiceEnvelopeScope::FutureAttacks;
        const bool affectsFuture = envelope->scope != VoiceEnvelopeScope::ActiveVoices;
        auto& lane = lanes[envelope->lane];
        if (affectsActive && lane.voiceEnd > envelope->header.tick &&
            activeEnvelopeWarnings.insert(warningSourceKey(envelope->header)).second) {
          diagnostics.push_back(variantWarning(
              "dynamic-envelope-active-voice",
              "A dynamic envelope update occurred during a sounding note; instrument variants apply it to "
              "future note attacks only",
              envelope->header));
        }
        if (affectsFuture) {
          applyEnvelopeUpdate(lane.envelope, envelope->update);
        }
        continue;
      }

      if (const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event);
          balance != nullptr && materializeStereo) {
        const bool active = std::ranges::any_of(lanes, [&](const auto& lane) {
          return lane.second.voiceEnd > balance->header.tick;
        });
        warnActiveStereoChange(balance->leftGain != leftGain || balance->rightGain != rightGain, active, balance->header);
        leftGain = balance->leftGain;
        rightGain = balance->rightGain;
        continue;
      }

      if (const auto* pan = std::get_if<ChannelPanPerformanceEvent>(&event); pan != nullptr && materializeStereo) {
        auto& lane = lanes[pan->lane];
        const double next = std::clamp(pan->position, 0.0, 1.0);
        warnActiveStereoChange(next != lane.pan, lane.voiceEnd > pan->header.tick, pan->header);
        lane.pan = next;
        continue;
      }

      auto* note = std::get_if<NotePerformanceEvent>(&event);
      if (note == nullptr) {
        continue;
      }

      const u64 noteEnd = addTicks(note->header.tick, note->durationTicks);
      auto& lane = lanes[note->lane];
      std::optional<InstrumentSelection> inherited;
      if (note->extendsPrevious) inherited = previous;
      if (const auto predecessor = continuedNotes.find(note->note); predecessor != continuedNotes.end()) {
        if (const auto found = attacks.find(predecessor->second); found != attacks.end()) inherited = found->second;
      }
      if (note->note.valid()) {
        if (const auto found = attacks.find(note->note); found != attacks.end()) inherited = found->second;
      }
      const bool continuesVoice = inherited.has_value();
      lane.voiceEnd = continuesVoice ? std::max(lane.voiceEnd, noteEnd) : noteEnd;
      if (continuesVoice) {
        note->instrument = *inherited;
        if (note->note.valid()) attacks.insert_or_assign(note->note, *inherited);
        previous = inherited;
        continue;
      }
      note->instrument = resolve(note->instrument.value_or(selected), &note->header);
      const auto remember = [&] {
        if (note->note.valid()) attacks.insert_or_assign(note->note, *note->instrument);
        previous = note->instrument;
      };

      const bool hasEnvelope = options.dynamicEnvelopes && lane.envelope.fields != EnvelopeFields::None;
      const bool envelopeOnly = hasEnvelope && !materializeStereo;
      const auto* baseRef = std::get_if<InstrumentHandle>(&*note->instrument);
      if (!baseRef) {
        if ((hasEnvelope || materializeStereo) && !warnedMissingInstrument) {
          diagnostics.push_back(instrumentNotFoundWarning(envelopeOnly, note->header));
          warnedMissingInstrument = true;
        }
        remember();
        continue;
      }

      const auto& base = soundBanks[baseRef->bank].instruments[baseRef->instrument];
      std::optional<InstrumentHandle> variantHandle;
      if (hasEnvelope || materializeStereo) {
        // Evaluate native responses before overriding them. Sample the whole
        // bank once, keeping its resolution independent of the variants added.
        if (sampledBanks.insert(baseRef->bank).second) {
          auto& bank = soundBanks[baseRef->bank];
          const u32 step = regionSamplingStep(bank, diagnostics);
          for (auto& instrument : bank.instruments) {
            if (std::ranges::any_of(instrument.regions,
                                    [](const Region& region) { return bool(region.response.evaluate); })) {
              instrument.regions = sampleRegionResponses(instrument.regions, step);
            }
          }
        }

        if (base.regions.empty()) {
          if (regionlessInstrumentWarnings.insert(*baseRef).second) {
            diagnostics.push_back(noRegionsWarning(envelopeOnly, note->header));
          }
        } else {
          std::vector<Region> regions;
          regions.reserve(base.regions.size() * (materializeStereo ? 2 : 1));
          for (auto region : base.regions) {
            if (hasEnvelope) {
              region.envelope = applyEnvelopeOverride(region.envelope, lane.envelope);
            }
            if (materializeStereo) {
              appendStereoLayers(regions, region, leftGain, rightGain, lane.pan);
            } else {
              regions.push_back(std::move(region));
            }
          }

          if (materializeStereo ||
              !std::ranges::equal(regions, base.regions, {}, &Region::envelope, &Region::envelope)) {
            if (const auto* existing = findVariant(variants, *baseRef, regions, soundBanks)) {
              variantHandle = existing->variant;
            } else {
              Instrument variant = base;
              variant.regions = std::move(regions);
              variant.identity.reset();
              variant.explicitAddress.reset();
              if (envelopeOnly) {
                variant.name = base.name.empty() ? "Dynamic envelope" : base.name + " [dynamic envelope]";
              } else {
                variant.name = base.name.empty() ? "Instrument variant" : base.name + " [variant]";
              }
              auto& bank = soundBanks[baseRef->bank];
              variantHandle = InstrumentHandle{baseRef->bank, static_cast<u32>(bank.instruments.size())};
              variants.push_back({*baseRef, *variantHandle});
              bank.instruments.push_back(std::move(variant));
            }
          }
        }
      }

      if (variantHandle) note->instrument = *variantHandle;
      remember();
    }

    if (materializeStereo) {
      std::erase_if(track.events, [](const PerformanceEvent& event) {
        return std::holds_alternative<StereoBalancePerformanceEvent>(event) ||
               std::holds_alternative<ChannelPanPerformanceEvent>(event);
      });
    }
  }
  return ResolvedPerformance{std::move(performance), std::move(soundBanks), initialInstrument};
}

InstrumentAddress InstrumentAddressPlan::address(const InstrumentSelection& selection) const {
  if (!valid) throw std::logic_error("Cannot use a failed instrument layout");
  if (const auto* handle = std::get_if<InstrumentHandle>(&selection)) return instruments.at(*handle);
  auto value = std::get<InstrumentAddress>(selection);
  if (!banks.empty()) value.bank = banks.at(value.bank);
  return value;
}

u32 InstrumentAddressPlan::nextBank() const {
  u32 next = 0;
  for (const auto& [source, target] : banks) next = std::max(next, target + 1);
  return next;
}

InstrumentAddressPlan planInstrumentAddresses(const ResolvedPerformance& performance, bool onlyUsed,
                                               std::optional<u32> compactBanks) {
  InstrumentAddressPlan result;
  std::set<InstrumentHandle> needed;
  std::set<std::pair<u32, u32>> external;
  const auto observe = [&](const InstrumentSelection& selection) {
    if (const auto* handle = std::get_if<InstrumentHandle>(&selection)) {
      needed.insert(*handle);
    } else {
      const auto value = std::get<InstrumentAddress>(selection);
      external.emplace(value.bank, value.program);
    }
  };
  for (const auto& track : performance.performance().tracks) {
    for (const auto& event : track.events) {
      if (const auto* change = std::get_if<InstrumentPerformanceEvent>(&event)) observe(change->instrument);
      if (const auto* note = std::get_if<NotePerformanceEvent>(&event); note && note->instrument) observe(*note->instrument);
    }
  }

  std::bitset<128 * 128> reserved;
  const auto reserve = [&](InstrumentAddress address) {
    const u32 program = std::min<u32>(address.program, 127);
    reserved.set((address.bank & 127) * 128 + program);
    const u32 sfBank = address.bank > 128 ? (address.bank >> 8) & 127 : address.bank;
    if (sfBank < 128) reserved.set(sfBank * 128 + program);
  };
  std::map<InstrumentHandle, std::optional<InstrumentAddress>> preferred;
  for (u32 bank = 0; bank < performance.soundBanks().size(); ++bank) {
    const auto& instruments = performance.soundBanks()[bank].instruments;
    for (u32 index = 0; index < instruments.size(); ++index) {
      const InstrumentHandle handle{bank, index};
      if (onlyUsed && !needed.contains(handle)) continue;
      const auto& instrument = instruments[index];
      const auto address = instrument.explicitAddress;
      preferred.emplace(handle, address);
      if (address) reserve(*address);
    }
  }
  for (const auto& [bank, program] : external) reserve({bank, program});

  u32 next = 0;
  std::set<std::pair<u32, u32>> assigned;
  for (const auto& [handle, preferredAddress] : preferred) {
    if (preferredAddress && assigned.emplace(preferredAddress->bank, preferredAddress->program).second) {
      result.instruments.emplace(handle, *preferredAddress);
      continue;
    }
    while (next < reserved.size() && reserved[next]) ++next;
    if (next == reserved.size()) {
      result.valid = false;
      result.diagnostics.push_back({.severity = Severity::Error, .code = "instrument-addresses-exhausted",
                                    .message = "Cannot allocate another portable bank/program address"});
      return result;
    }
    reserved.set(next);
    result.instruments.emplace(handle, InstrumentAddress{next / 128, next % 128});
    ++next;
  }
  if (compactBanks) {
    std::set<u32> sourceBanks{0};
    for (const auto& [handle, address] : result.instruments) sourceBanks.insert(address.bank);
    for (const auto& [bank, program] : external) sourceBanks.insert(bank);
    if (*compactBanks > 128 || sourceBanks.size() > 128 - *compactBanks) {
      result.valid = false;
      result.diagnostics.push_back({.severity = Severity::Error, .code = "instrument-banks-exhausted",
                                    .message = "Stitched collections require more than 128 preset banks"});
      return result;
    }
    u32 bank = *compactBanks;
    for (const u32 source : sourceBanks) result.banks.emplace(source, bank++);
    for (auto& [handle, address] : result.instruments) address.bank = result.banks.at(address.bank);
  }
  return result;
}

}  // namespace vgmtrans::core
