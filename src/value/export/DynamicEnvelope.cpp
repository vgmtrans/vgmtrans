/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/DynamicEnvelope.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <string>
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

[[nodiscard]] Diagnostic envelopeWarning(std::string code, std::string message, const PerformanceEventHeader& header) {
  return Diagnostic{
      .severity = Severity::Warning,
      .code = std::move(code),
      .message = std::move(message),
      .annotation = header.sourceAnnotation.valid() ? std::optional{header.sourceAnnotation} : std::nullopt,
  };
}

[[nodiscard]] u64 warningSourceKey(const PerformanceEventHeader& header) noexcept {
  if (header.sourceAnnotation.valid()) {
    return header.sourceAnnotation.value;
  }
  if (header.sourceCommand.valid()) {
    return (u64{1} << 63) | header.sourceCommand.value;
  }
  return (u64{1} << 62) ^ (static_cast<u64>(header.track.value) << 32) ^ header.sequence;
}

template <typename Predicate>
[[nodiscard]] std::optional<InstrumentRef> findInstrument(std::span<const SoundBankAsset> soundBanks,
                                                          Predicate matches) {
  for (u32 setIndex = 0; setIndex < soundBanks.size(); ++setIndex) {
    for (const auto& instrument : soundBanks[setIndex].instruments) {
      if (matches(instrument)) {
        return InstrumentRef{.set = setIndex, .instrument = &instrument};
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] InstrumentAddress instrumentAddress(InstrumentRef ref) {
  return resolveInstrumentAddress(ref.instrument->explicitAddress, ref.instrument->identity);
}

[[nodiscard]] std::optional<InstrumentRef> resolveSelection(const InstrumentPerformanceEvent& selection,
                                                            std::span<const SoundBankAsset> soundBanks) {
  if (selection.sourceInstrument) {
    if (auto resolved = findInstrument(soundBanks, [&](const Instrument& instrument) {
          return instrument.identity && *instrument.identity == *selection.sourceInstrument;
        })) {
      return resolved;
    }
    const auto fallback = resolveInstrumentAddress({}, selection.sourceInstrument);
    return findInstrument(soundBanks, [&](const Instrument& instrument) {
      return resolveInstrumentAddress(instrument.explicitAddress, instrument.identity) == fallback;
    });
  }

  const InstrumentAddress address{.bank = selection.bank, .program = selection.program};
  return findInstrument(soundBanks, [&](const Instrument& instrument) {
    return resolveInstrumentAddress(instrument.explicitAddress, instrument.identity) == address;
  });
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
        const auto* selection = std::get_if<InstrumentPerformanceEvent>(&event);
        if (selection == nullptr) {
          continue;
        }
        if (selection->sourceInstrument) {
          reserve(resolveInstrumentAddress({}, selection->sourceInstrument));
        } else {
          reserve(InstrumentAddress{
              .bank = selection->bank,
              .program = selection->program,
          });
        }
      }
    }
  }

  [[nodiscard]] std::optional<InstrumentAddress> allocate() {
    for (u32 bank = 0; bank < 128; ++bank) {
      for (u32 program = 0; program < 128; ++program) {
        if (used_.insert({bank, program}).second) {
          return InstrumentAddress{.bank = bank, .program = program};
        }
      }
    }
    return std::nullopt;
  }

private:
  void reserve(InstrumentAddress address) {
    const u32 program = std::min<u32>(address.program, 127);
    // MIDI and DLS retain the low seven bank bits.
    used_.insert({address.bank & 0x7f, program});
    // SF2's established lowering treats larger logical banks as a packed
    // high-byte value. Reserve that projection too.
    const u32 soundFontBank = address.bank > 128 ? (address.bank >> 8) & 0x7f : address.bank;
    if (soundFontBank < 128) {
      used_.insert({soundFontBank, program});
    }
  }

  std::set<std::pair<u32, u32>> used_;
};

struct VariantRecord {
  InstrumentRef base;
  InstrumentAddress address;
  Instrument instrument;
};

[[nodiscard]] const VariantRecord* findVariant(std::span<const VariantRecord> variants, InstrumentRef base,
                                               const Instrument& candidate) {
  const auto found = std::ranges::find_if(variants, [&](const VariantRecord& variant) {
    return variant.base == base &&
           std::ranges::equal(variant.instrument.regions, candidate.regions,
                              [](const Region& left, const Region& right) { return left.envelope == right.envelope; });
  });
  return found == variants.end() ? nullptr : &*found;
}

}  // namespace

DynamicEnvelopeMaterialization materializeDynamicEnvelopes(const PerformanceSequence& performance,
                                                           std::span<SoundBankAsset> soundBanks) {
  DynamicEnvelopeMaterialization result{
      .performance = performance,
  };
  AddressAllocator addresses{soundBanks, performance};
  std::vector<VariantRecord> variants;
  std::set<u64> activeVoiceWarnings;
  std::set<const Instrument*> regionlessInstrumentWarnings;

  for (auto& track : result.performance.tracks) {
    auto selectedInstrument = resolveSelection({}, soundBanks);
    std::unordered_map<PerformanceLaneId, EnvelopeOverride> envelopeStates;
    std::unordered_map<PerformanceLaneId, u64> voiceEnds;
    bool warnedMissingInstrument = false;
    bool variantSelected = false;

    for (auto& event : track.events) {
      if (const auto* selection = std::get_if<InstrumentPerformanceEvent>(&event)) {
        selectedInstrument = resolveSelection(*selection, soundBanks);
        variantSelected = false;
        if (selection->envelopeMode == InstrumentEnvelopeMode::UseInstrumentEnvelope) {
          envelopeStates.clear();
        }
        continue;
      }

      if (const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event)) {
        if (!validEnvelopeUpdate(envelope->update)) {
          result.diagnostics.push_back(envelopeWarning("dynamic-envelope-invalid",
                                                       "Ignored an invalid dynamic envelope update", envelope->header));
          continue;
        }

        const bool affectsActive = envelope->scope != VoiceEnvelopeScope::FutureAttacks;
        const bool affectsFuture = envelope->scope != VoiceEnvelopeScope::ActiveVoices;
        const auto voiceEnd = voiceEnds.find(envelope->lane);
        if (affectsActive && voiceEnd != voiceEnds.end() && voiceEnd->second > envelope->header.tick &&
            activeVoiceWarnings.insert(warningSourceKey(envelope->header)).second) {
          result.diagnostics.push_back(envelopeWarning(
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

      auto* note = std::get_if<NotePerformanceEvent>(&event);
      if (note == nullptr) {
        continue;
      }

      const u64 noteEnd = note->header.tick > std::numeric_limits<u64>::max() - note->durationTicks
                              ? std::numeric_limits<u64>::max()
                              : note->header.tick + note->durationTicks;
      auto& voiceEnd = voiceEnds[note->lane];
      voiceEnd = note->extendsPrevious ? std::max(voiceEnd, noteEnd) : noteEnd;
      if (note->extendsPrevious) {
        continue;
      }

      const auto state = envelopeStates.find(note->lane);
      const bool hasOverride = state != envelopeStates.end() && state->second.fields != EnvelopeFields::None;
      const auto baseRef = selectedInstrument;
      if (!baseRef) {
        if (hasOverride && !warnedMissingInstrument) {
          result.diagnostics.push_back(envelopeWarning(
              "dynamic-envelope-instrument-not-found",
              "Could not resolve the selected instrument for a dynamic envelope variant", note->header));
          warnedMissingInstrument = true;
        }
        continue;
      }

      InstrumentAddress selectedAddress = instrumentAddress(*baseRef);
      bool usesVariant = false;
      if (hasOverride) {
        const auto& base = *baseRef->instrument;
        if (base.regions.empty()) {
          if (regionlessInstrumentWarnings.insert(&base).second) {
            result.diagnostics.push_back(envelopeWarning(
                "dynamic-envelope-no-regions",
                "Could not apply a dynamic envelope to an instrument without sampled regions", note->header));
          }
        } else {
          Instrument variant = base;
          bool differs = false;
          for (auto& region : variant.regions) {
            const auto effective = applyEnvelopeOverride(region.envelope, state->second);
            differs = differs || effective != region.envelope;
            region.envelope = effective;
          }

          if (differs) {
            if (const auto* existing = findVariant(variants, *baseRef, variant)) {
              selectedAddress = existing->address;
              usesVariant = true;
            } else if (const auto address = addresses.allocate()) {
              variant.identity.reset();
              variant.explicitAddress = *address;
              variant.name = base.name.empty() ? "Dynamic envelope" : base.name + " [dynamic envelope]";
              selectedAddress = *address;
              usesVariant = true;
              variants.push_back(VariantRecord{
                  .base = *baseRef,
                  .address = *address,
                  .instrument = std::move(variant),
              });
            } else {
              result.diagnostics.push_back(envelopeWarning(
                  "dynamic-envelope-addresses-exhausted",
                  "Could not allocate another portable bank/program address for a dynamic envelope variant",
                  note->header));
            }
          }
        }
      }

      if (usesVariant || variantSelected) {
        note->instrumentAddress = selectedAddress;
      }
      variantSelected = usesVariant;
    }
  }

  for (auto& variant : variants) {
    soundBanks[variant.base.set].instruments.push_back(std::move(variant.instrument));
  }
  return result;
}

}  // namespace vgmtrans::core
