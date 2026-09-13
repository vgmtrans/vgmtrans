/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/InstrumentVariants.h"
#include "value/export/PerformanceInstrumentSelection.h"
#include "value/export/synth/SynthExportData.h"
#include "value/synth/SynthMath.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace vgmtrans::core {

namespace {

struct EnvelopeOverride {
  Envelope values;
  EnvelopeFields fields = EnvelopeFields::None;
};

struct InstrumentRef {
  u32 set = invalidIdValue;
  const Instrument* instrument = nullptr;

  friend bool operator==(const InstrumentRef&, const InstrumentRef&) noexcept = default;
};

using EnvelopeMember = std::optional<double> Envelope::*;

struct EnvelopeField {
  EnvelopeFields field;
  EnvelopeMember member;
};

constexpr std::array envelopeFields{
    EnvelopeField{EnvelopeFields::Attack, &Envelope::attackSeconds},
    EnvelopeField{EnvelopeFields::Hold, &Envelope::holdSeconds},
    EnvelopeField{EnvelopeFields::Decay, &Envelope::decaySeconds},
    EnvelopeField{EnvelopeFields::SecondDecay, &Envelope::secondDecaySeconds},
    EnvelopeField{EnvelopeFields::Release, &Envelope::releaseSeconds},
    EnvelopeField{EnvelopeFields::Sustain, &Envelope::sustainAmplitude},
};

void applyEnvelopeUpdate(EnvelopeOverride& state, const EnvelopeUpdate& update) {
  if (!update.values) {
    state.fields = static_cast<EnvelopeFields>(static_cast<u8>(state.fields) & ~static_cast<u8>(update.fields));
    return;
  }
  for (const auto [field, member] : envelopeFields) {
    if (hasEnvelopeField(update.fields, field)) {
      state.values.*member = (*update.values).*member;
    }
  }
  state.fields |= update.fields;
}

[[nodiscard]] Envelope applyEnvelopeOverride(Envelope envelope, const EnvelopeOverride& state) {
  for (const auto [field, member] : envelopeFields) {
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
  for (const auto [field, member] : envelopeFields) {
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

[[nodiscard]] Diagnostic addressesExhaustedWarning(bool dynamicEnvelopeOnly, const PerformanceEventHeader& header) {
  if (dynamicEnvelopeOnly) {
    return variantWarning("dynamic-envelope-addresses-exhausted",
                          "Could not allocate another portable bank/program address for a dynamic envelope variant",
                          header);
  }
  return variantWarning("instrument-variant-addresses-exhausted",
                        "Could not allocate another portable bank/program address for an instrument variant", header);
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

[[nodiscard]] std::optional<InstrumentRef> findInstrument(std::span<const SoundBankAsset> soundBanks,
                                                          const InstrumentSelection& selection) {
  for (u32 setIndex = 0; setIndex < soundBanks.size(); ++setIndex) {
    for (const auto& instrument : soundBanks[setIndex].instruments) {
      if (matchesInstrumentSelection(instrument, selection)) {
        return InstrumentRef{.set = setIndex, .instrument = &instrument};
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<InstrumentRef> resolveSelection(const InstrumentSelection& selection,
                                                            std::span<const SoundBankAsset> soundBanks) {
  if (auto resolved = findInstrument(soundBanks, selection);
      resolved || std::holds_alternative<InstrumentAddress>(selection)) {
    return resolved;
  }
  return findInstrument(soundBanks, resolveInstrumentAddress(selection));
}

class AddressAllocator {
public:
  AddressAllocator(std::span<const SoundBankAsset> soundBanks, const PerformanceSequence& performance) {
    for (const auto& soundBank : soundBanks) {
      for (const auto& instrument : soundBank.instruments) {
        reserve(resolveInstrumentAddress(instrument.explicitAddress, instrument.identity));
      }
    }
    for (const auto& track : performance.tracks) {
      for (const auto& event : track.events) {
        if (const auto* selection = std::get_if<InstrumentPerformanceEvent>(&event)) {
          reserve(resolveInstrumentAddress(selection->instrument));
        } else if (const auto* note = std::get_if<NotePerformanceEvent>(&event); note && note->instrumentAddress) {
          reserve(*note->instrumentAddress);
        }
      }
    }
  }

  [[nodiscard]] std::optional<InstrumentAddress> allocate() {
    while (next_ < reserved_.size() && reserved_[next_]) {
      ++next_;
    }
    if (next_ == reserved_.size()) {
      return std::nullopt;
    }
    const u32 address = next_++;
    return InstrumentAddress{.bank = address / 128, .program = address % 128};
  }

private:
  void reserve(InstrumentAddress address) {
    const u32 program = std::min<u32>(address.program, 127);
    // MIDI and DLS retain the low seven bank bits.
    reserved_.set((address.bank & 0x7f) * 128 + program);
    // SF2's established lowering treats larger logical banks as a packed
    // high-byte value. Reserve that projection too.
    const u32 soundFontBank = address.bank > 128 ? (address.bank >> 8) & 0x7f : address.bank;
    if (soundFontBank < 128) {
      reserved_.set(soundFontBank * 128 + program);
    }
  }

  std::bitset<128 * 128> reserved_;
  u32 next_ = 0;
};

struct VariantRecord {
  InstrumentRef base;
  Instrument instrument;
};

[[nodiscard]] bool sameVariantRegion(const Region& left, const Region& right) {
  return left.envelope == right.envelope && left.pan == right.pan && left.attenuationDb == right.attenuationDb &&
         left.invertSamplePhase == right.invertSamplePhase;
}

[[nodiscard]] const VariantRecord* findVariant(std::span<const VariantRecord> variants, InstrumentRef base,
                                               std::span<const Region> regions) {
  const auto found = std::ranges::find_if(variants, [&](const VariantRecord& variant) {
    return variant.base == base && std::ranges::equal(variant.instrument.regions, regions, sameVariantRegion);
  });
  return found == variants.end() ? nullptr : &*found;
}

[[nodiscard]] bool requiresSignedStereoVariants(const PerformanceTrack& track) {
  return std::ranges::any_of(track.events, [](const PerformanceEvent& event) {
    const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event);
    return balance != nullptr && (balance->leftGain < 0.0 || balance->rightGain < 0.0);
  });
}

[[nodiscard]] bool hasActiveVoice(const std::unordered_map<PerformanceLaneId, u64>& voiceEnds, u64 tick,
                                  std::optional<PerformanceLaneId> lane = std::nullopt) {
  if (lane) {
    const auto found = voiceEnds.find(*lane);
    return found != voiceEnds.end() && found->second > tick;
  }
  return std::ranges::any_of(voiceEnds, [tick](const auto& voice) { return voice.second > tick; });
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

}  // namespace

InstrumentVariantMaterialization materializeInstrumentVariants(const PerformanceSequence& performance,
                                                               std::span<SoundBankAsset> soundBanks,
                                                               InstrumentVariantOptions options) {
  InstrumentVariantMaterialization result{.performance = performance};
  for (auto& bank : soundBanks) {
    const u32 step = regionSamplingStep(bank, result.diagnostics);
    for (auto& instrument : bank.instruments) {
      if (std::ranges::any_of(instrument.regions,
                              [](const Region& region) { return bool(region.response.evaluate); })) {
        instrument.regions = sampleRegionResponses(instrument.regions, step);
      }
    }
  }
  AddressAllocator addresses{soundBanks, performance};
  std::vector<VariantRecord> variants;
  std::set<WarningSourceKey> activeEnvelopeWarnings;
  std::set<WarningSourceKey> activeStereoWarnings;
  std::set<const Instrument*> regionlessInstrumentWarnings;

  for (auto& track : result.performance.tracks) {
    const auto continuedNotes = performanceNotePredecessors(track);
    // Once a track uses signed channel gain, all of its pan must be baked into
    // variants so ordinary MIDI pan does not also affect the layered output.
    const bool materializeStereo = options.signedStereo && requiresSignedStereoVariants(track);
    auto selectedInstrument = resolveSelection({}, soundBanks);
    std::unordered_map<PerformanceLaneId, EnvelopeOverride> envelopeStates;
    std::unordered_map<PerformanceLaneId, double> pans;
    std::unordered_map<PerformanceLaneId, u64> voiceEnds;
    double leftGain = 1.0;
    double rightGain = 1.0;
    bool warnedMissingInstrument = false;
    bool previousAttackUsedVariant = false;
    const auto warnActiveStereoChange = [&](bool changed, const PerformanceEventHeader& header,
                                            std::optional<PerformanceLaneId> lane = std::nullopt) {
      if (changed && hasActiveVoice(voiceEnds, header.tick, lane) &&
          activeStereoWarnings.insert(warningSourceKey(header)).second) {
        result.diagnostics.push_back(variantWarning(
            "signed-stereo-active-voice",
            "A phase or pan change occurred during a sounding note; stereo instrument variants apply it to "
            "future note attacks only",
            header));
      }
    };

    for (auto& event : track.events) {
      if (const auto* selection = std::get_if<InstrumentPerformanceEvent>(&event)) {
        selectedInstrument = resolveSelection(selection->instrument, soundBanks);
        previousAttackUsedVariant = false;
        if (selection->envelopeMode == InstrumentEnvelopeMode::UseInstrumentEnvelope) {
          envelopeStates.clear();
        }
        continue;
      }

      if (const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event);
          envelope != nullptr && options.dynamicEnvelopes) {
        if (!validEnvelopeUpdate(envelope->update)) {
          result.diagnostics.push_back(variantWarning("dynamic-envelope-invalid",
                                                      "Ignored an invalid dynamic envelope update", envelope->header));
          continue;
        }

        const bool affectsActive = envelope->scope != VoiceEnvelopeScope::FutureAttacks;
        const bool affectsFuture = envelope->scope != VoiceEnvelopeScope::ActiveVoices;
        if (affectsActive && hasActiveVoice(voiceEnds, envelope->header.tick, envelope->lane) &&
            activeEnvelopeWarnings.insert(warningSourceKey(envelope->header)).second) {
          result.diagnostics.push_back(variantWarning(
              "dynamic-envelope-active-voice",
              "A dynamic envelope update occurred during a sounding note; instrument variants apply it to "
              "future note attacks only",
              envelope->header));
        }
        if (affectsFuture) {
          applyEnvelopeUpdate(envelopeStates[envelope->lane], envelope->update);
        }
        continue;
      }

      if (const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event);
          balance != nullptr && materializeStereo) {
        warnActiveStereoChange(balance->leftGain != leftGain || balance->rightGain != rightGain, balance->header);
        leftGain = balance->leftGain;
        rightGain = balance->rightGain;
        continue;
      }

      if (const auto* pan = std::get_if<ChannelPanPerformanceEvent>(&event); pan != nullptr && materializeStereo) {
        auto& position = pans.try_emplace(pan->lane, 0.5).first->second;
        const double next = std::clamp(pan->position, 0.0, 1.0);
        warnActiveStereoChange(next != position, pan->header, pan->lane);
        position = next;
        continue;
      }

      auto* note = std::get_if<NotePerformanceEvent>(&event);
      if (note == nullptr) {
        continue;
      }

      const u64 noteEnd = addTicks(note->header.tick, note->durationTicks);
      auto& voiceEnd = voiceEnds[note->lane];
      const bool continuesVoice = note->extendsPrevious || continuedNotes.contains(note->note);
      voiceEnd = continuesVoice ? std::max(voiceEnd, noteEnd) : noteEnd;
      if (continuesVoice) {
        continue;
      }

      const auto envelope = envelopeStates.find(note->lane);
      const bool hasEnvelope = options.dynamicEnvelopes && envelope != envelopeStates.end() &&
                               envelope->second.fields != EnvelopeFields::None;
      const bool envelopeOnly = hasEnvelope && !materializeStereo;
      const auto baseRef =
          note->instrumentAddress ? resolveSelection(*note->instrumentAddress, soundBanks) : selectedInstrument;
      if (!baseRef) {
        if ((hasEnvelope || materializeStereo) && !warnedMissingInstrument) {
          result.diagnostics.push_back(instrumentNotFoundWarning(envelopeOnly, note->header));
          warnedMissingInstrument = true;
        }
        continue;
      }

      const auto& base = *baseRef->instrument;
      std::optional<InstrumentAddress> variantAddress;
      if (hasEnvelope || materializeStereo) {
        if (base.regions.empty()) {
          if (regionlessInstrumentWarnings.insert(&base).second) {
            result.diagnostics.push_back(noRegionsWarning(envelopeOnly, note->header));
          }
        } else {
          std::vector<Region> regions;
          regions.reserve(base.regions.size() * (materializeStereo ? 2 : 1));
          const double pan = materializeStereo ? pans.try_emplace(note->lane, 0.5).first->second : 0.5;
          for (auto region : base.regions) {
            if (hasEnvelope) {
              region.envelope = applyEnvelopeOverride(region.envelope, envelope->second);
            }
            if (materializeStereo) {
              appendStereoLayers(regions, region, leftGain, rightGain, pan);
            } else {
              regions.push_back(std::move(region));
            }
          }

          if (materializeStereo ||
              !std::ranges::equal(regions, base.regions, {}, &Region::envelope, &Region::envelope)) {
            if (const auto* existing = findVariant(variants, *baseRef, regions)) {
              variantAddress = existing->instrument.explicitAddress;
            } else if (const auto address = addresses.allocate()) {
              Instrument variant = base;
              variant.regions = std::move(regions);
              variant.identity.reset();
              variant.explicitAddress = *address;
              if (envelopeOnly) {
                variant.name = base.name.empty() ? "Dynamic envelope" : base.name + " [dynamic envelope]";
              } else {
                variant.name = base.name.empty() ? "Instrument variant" : base.name + " [variant]";
              }
              variantAddress = *address;
              variants.push_back(VariantRecord{
                  .base = *baseRef,
                  .instrument = std::move(variant),
              });
            } else {
              result.diagnostics.push_back(addressesExhaustedWarning(envelopeOnly, note->header));
            }
          }
        }
      }

      if (variantAddress || previousAttackUsedVariant) {
        note->instrumentAddress =
            variantAddress.value_or(resolveInstrumentAddress(base.explicitAddress, base.identity));
      }
      previousAttackUsedVariant = variantAddress.has_value();
    }

    if (materializeStereo) {
      std::erase_if(track.events, [](const PerformanceEvent& event) {
        return std::holds_alternative<StereoBalancePerformanceEvent>(event) ||
               std::holds_alternative<ChannelPanPerformanceEvent>(event);
      });
    }
  }

  for (auto& variant : variants) {
    soundBanks[variant.base.set].instruments.push_back(std::move(variant.instrument));
  }
  return result;
}

}  // namespace vgmtrans::core
