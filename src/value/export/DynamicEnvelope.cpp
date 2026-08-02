/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/DynamicEnvelope.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

struct PreparedInstrumentRef {
  u32 set = invalidIdValue;
  u32 instrument = invalidIdValue;

  friend bool operator==(const PreparedInstrumentRef&, const PreparedInstrumentRef&) noexcept = default;
};

template <typename Member>
void copyEnvelopeField(Envelope& destination, const Envelope& source, Member member, EnvelopeFields fields,
                       EnvelopeFields field) {
  if (hasEnvelopeField(fields, field)) {
    destination.*member = source.*member;
  }
}

void applyEnvelopeUpdate(EnvelopeOverride& state, const EnvelopeUpdate& update) {
  const auto inherit = [&](auto member, EnvelopeFields field) {
    if (hasEnvelopeField(update.inheritFields, field)) {
      state.fields = static_cast<EnvelopeFields>(static_cast<u8>(state.fields) & ~static_cast<u8>(field));
      state.values.*member = std::nullopt;
    }
  };
  const auto set = [&](auto member, EnvelopeFields field) {
    if (hasEnvelopeField(update.setFields, field)) {
      state.fields |= field;
      state.values.*member = update.values.*member;
    }
  };

  inherit(&Envelope::attackSeconds, EnvelopeFields::Attack);
  inherit(&Envelope::holdSeconds, EnvelopeFields::Hold);
  inherit(&Envelope::decaySeconds, EnvelopeFields::Decay);
  inherit(&Envelope::secondDecaySeconds, EnvelopeFields::SecondDecay);
  inherit(&Envelope::releaseSeconds, EnvelopeFields::Release);
  inherit(&Envelope::sustainAmplitude, EnvelopeFields::Sustain);
  set(&Envelope::attackSeconds, EnvelopeFields::Attack);
  set(&Envelope::holdSeconds, EnvelopeFields::Hold);
  set(&Envelope::decaySeconds, EnvelopeFields::Decay);
  set(&Envelope::secondDecaySeconds, EnvelopeFields::SecondDecay);
  set(&Envelope::releaseSeconds, EnvelopeFields::Release);
  set(&Envelope::sustainAmplitude, EnvelopeFields::Sustain);
}

[[nodiscard]] Envelope applyEnvelopeOverride(Envelope envelope, const EnvelopeOverride& state) {
  copyEnvelopeField(envelope, state.values, &Envelope::attackSeconds, state.fields, EnvelopeFields::Attack);
  copyEnvelopeField(envelope, state.values, &Envelope::holdSeconds, state.fields, EnvelopeFields::Hold);
  copyEnvelopeField(envelope, state.values, &Envelope::decaySeconds, state.fields, EnvelopeFields::Decay);
  copyEnvelopeField(envelope, state.values, &Envelope::secondDecaySeconds, state.fields, EnvelopeFields::SecondDecay);
  copyEnvelopeField(envelope, state.values, &Envelope::releaseSeconds, state.fields, EnvelopeFields::Release);
  copyEnvelopeField(envelope, state.values, &Envelope::sustainAmplitude, state.fields, EnvelopeFields::Sustain);
  return envelope;
}

[[nodiscard]] bool validDuration(const std::optional<double>& value) {
  return !value || (!std::isnan(*value) && *value >= 0.0);
}

[[nodiscard]] bool validEnvelopeUpdate(const EnvelopeUpdate& update) {
  constexpr u8 knownFields = static_cast<u8>(EnvelopeFields::All);
  const u8 setFields = static_cast<u8>(update.setFields);
  const u8 inheritFields = static_cast<u8>(update.inheritFields);
  if ((setFields & inheritFields) != 0 || ((setFields | inheritFields) & ~knownFields) != 0) {
    return false;
  }
  if (hasEnvelopeField(update.setFields, EnvelopeFields::Attack) && !validDuration(update.values.attackSeconds)) {
    return false;
  }
  if (hasEnvelopeField(update.setFields, EnvelopeFields::Hold) && !validDuration(update.values.holdSeconds)) {
    return false;
  }
  if (hasEnvelopeField(update.setFields, EnvelopeFields::Decay) && !validDuration(update.values.decaySeconds)) {
    return false;
  }
  if (hasEnvelopeField(update.setFields, EnvelopeFields::SecondDecay) &&
      !validDuration(update.values.secondDecaySeconds)) {
    return false;
  }
  if (hasEnvelopeField(update.setFields, EnvelopeFields::Release) && !validDuration(update.values.releaseSeconds)) {
    return false;
  }
  if (hasEnvelopeField(update.setFields, EnvelopeFields::Sustain)) {
    const auto sustain = update.values.sustainAmplitude;
    if (sustain && (!std::isfinite(*sustain) || *sustain < 0.0 || *sustain > 1.0)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Diagnostic envelopeWarning(std::string code, std::string message,
                                         const PerformanceEventHeader* header = nullptr) {
  Diagnostic diagnostic{
      .severity = Severity::Warning,
      .code = std::move(code),
      .message = std::move(message),
  };
  if (header != nullptr && header->sourceAnnotation.valid()) {
    diagnostic.annotation = header->sourceAnnotation;
  }
  return diagnostic;
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
[[nodiscard]] std::optional<PreparedInstrumentRef> findInstrument(std::span<const InstrumentSetAsset> instrumentSets,
                                                                  Predicate matches) {
  for (u32 setIndex = 0; setIndex < instrumentSets.size(); ++setIndex) {
    const auto& instruments = instrumentSets[setIndex].instruments;
    for (u32 instrumentIndex = 0; instrumentIndex < instruments.size(); ++instrumentIndex) {
      if (matches(instruments[instrumentIndex])) {
        return PreparedInstrumentRef{.set = setIndex, .instrument = instrumentIndex};
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<PreparedInstrumentRef> resolveInstrument(const InstrumentPerformanceEvent& selection,
                                                                     std::span<const InstrumentSetAsset> instrumentSets) {
  if (selection.sourceInstrument) {
    if (const auto exact = findInstrument(instrumentSets, [&](const Instrument& instrument) {
          return instrument.identity && *instrument.identity == *selection.sourceInstrument;
        })) {
      return exact;
    }

    const auto fallbackAddress = resolveInstrumentAddress({}, selection.sourceInstrument);
    return findInstrument(instrumentSets, [&](const Instrument& instrument) {
      return resolveInstrumentAddress(instrument.explicitAddress, instrument.identity) == fallbackAddress;
    });
  }

  const InstrumentAddress address{.bank = selection.bank, .program = selection.program};
  return findInstrument(instrumentSets, [&](const Instrument& instrument) {
    return resolveInstrumentAddress(instrument.explicitAddress, instrument.identity) == address;
  });
}

[[nodiscard]] InstrumentAddress preparedInstrumentAddress(PreparedInstrumentRef ref,
                                                          std::span<const InstrumentSetAsset> instrumentSets) {
  const auto& instrument = instrumentSets[ref.set].instruments[ref.instrument];
  return resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
}

[[nodiscard]] InstrumentAddress selectionAddress(const InstrumentPerformanceEvent& selection,
                                                 std::optional<PreparedInstrumentRef> resolved,
                                                 std::span<const InstrumentSetAsset> instrumentSets) {
  if (resolved) {
    return preparedInstrumentAddress(*resolved, instrumentSets);
  }
  if (selection.sourceInstrument) {
    return resolveInstrumentAddress({}, selection.sourceInstrument);
  }
  return InstrumentAddress{.bank = selection.bank, .program = selection.program};
}

using TargetAddress = std::pair<u32, u32>;

class AddressAllocator {
public:
  AddressAllocator(std::span<const InstrumentSetAsset> instrumentSets, const PerformanceSequence& performance) {
    for (const auto& instrumentSet : instrumentSets) {
      for (const auto& instrument : instrumentSet.instruments) {
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

  std::set<TargetAddress> used_;
};

struct VariantRecord {
  PreparedInstrumentRef base;
  std::vector<Envelope> envelopes;
  PreparedInstrumentRef variant;
};

[[nodiscard]] std::optional<PreparedInstrumentRef> findVariant(std::span<const VariantRecord> variants,
                                                               PreparedInstrumentRef base,
                                                               std::span<const Envelope> envelopes) {
  const auto found = std::ranges::find_if(variants, [&](const VariantRecord& variant) {
    return variant.base == base && std::ranges::equal(variant.envelopes, envelopes);
  });
  if (found == variants.end()) {
    return std::nullopt;
  }
  return found->variant;
}

}  // namespace

DynamicEnvelopeMaterialization materializeDynamicEnvelopes(const PerformanceSequence& performance,
                                                           std::span<InstrumentSetAsset> instrumentSets) {
  DynamicEnvelopeMaterialization result{
      .performance = performance,
  };
  AddressAllocator addresses{instrumentSets, performance};
  std::vector<VariantRecord> variants;
  std::set<u64> activeVoiceWarnings;
  std::set<u64> regionlessInstrumentWarnings;
  std::set<u32> missingInstrumentWarnings;

  for (auto& track : result.performance.tracks) {
    auto sourceEvents = std::move(track.events);
    std::ranges::stable_sort(sourceEvents, [](const PerformanceEvent& left, const PerformanceEvent& right) {
      const auto& leftHeader = performanceEventHeader(left);
      const auto& rightHeader = performanceEventHeader(right);
      return std::tie(leftHeader.tick, leftHeader.sequence) < std::tie(rightHeader.tick, rightHeader.sequence);
    });
    std::vector<PerformanceEvent> loweredEvents;
    loweredEvents.reserve(sourceEvents.size());

    InstrumentPerformanceEvent selectedInstrument;
    InstrumentAddress outputAddress;
    std::unordered_map<PerformanceLaneId, EnvelopeOverride> envelopeStates;
    std::unordered_map<PerformanceLaneId, u64> voiceEnds;

    for (const auto& event : sourceEvents) {
      if (const auto* selection = std::get_if<InstrumentPerformanceEvent>(&event)) {
        selectedInstrument = *selection;
        const auto resolved = resolveInstrument(*selection, instrumentSets);
        outputAddress = selectionAddress(*selection, resolved, instrumentSets);
        if (selection->envelopeMode == InstrumentEnvelopeMode::UseInstrumentEnvelope) {
          envelopeStates.clear();
        }
        loweredEvents.push_back(event);
        continue;
      }

      if (const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event)) {
        if (!validEnvelopeUpdate(envelope->update)) {
          result.diagnostics.push_back(envelopeWarning(
              "dynamic-envelope-invalid", "Ignored an invalid dynamic envelope update", &envelope->header));
          loweredEvents.push_back(event);
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
              &envelope->header));
        }
        if (affectsFuture) {
          applyEnvelopeUpdate(envelopeStates[envelope->lane], envelope->update);
        }
        loweredEvents.push_back(event);
        continue;
      }

      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      if (note == nullptr) {
        loweredEvents.push_back(event);
        continue;
      }

      const u64 noteEnd = note->header.tick > std::numeric_limits<u64>::max() - note->durationTicks
                              ? std::numeric_limits<u64>::max()
                              : note->header.tick + note->durationTicks;
      auto& voiceEnd = voiceEnds[note->lane];
      voiceEnd = note->extendsPrevious ? std::max(voiceEnd, noteEnd) : noteEnd;
      if (note->extendsPrevious) {
        loweredEvents.push_back(event);
        continue;
      }

      const auto state = envelopeStates.find(note->lane);
      const bool hasOverride = state != envelopeStates.end() && state->second.fields != EnvelopeFields::None;
      const auto baseRef = resolveInstrument(selectedInstrument, instrumentSets);
      if (!baseRef) {
        if (hasOverride && missingInstrumentWarnings.insert(track.id.value).second) {
          result.diagnostics.push_back(envelopeWarning(
              "dynamic-envelope-instrument-not-found",
              "Could not resolve the selected instrument for a dynamic envelope variant", &note->header));
        }
        loweredEvents.push_back(event);
        continue;
      }

      PreparedInstrumentRef selectedRef = *baseRef;
      if (hasOverride) {
        const auto& base = instrumentSets[baseRef->set].instruments[baseRef->instrument];
        if (base.regions.empty()) {
          const u64 instrumentKey = (static_cast<u64>(baseRef->set) << 32) | baseRef->instrument;
          if (regionlessInstrumentWarnings.insert(instrumentKey).second) {
            result.diagnostics.push_back(envelopeWarning(
                "dynamic-envelope-no-regions",
                "Could not apply a dynamic envelope to an instrument without sampled regions", &note->header));
          }
        } else {
          std::vector<Envelope> effectiveEnvelopes;
          effectiveEnvelopes.reserve(base.regions.size());
          bool differs = false;
          for (const auto& region : base.regions) {
            auto effective = applyEnvelopeOverride(region.envelope, state->second);
            differs = differs || effective != region.envelope;
            effectiveEnvelopes.push_back(std::move(effective));
          }

          if (differs) {
            if (const auto existing = findVariant(variants, *baseRef, effectiveEnvelopes)) {
              selectedRef = *existing;
            } else if (const auto address = addresses.allocate()) {
              Instrument variant = base;
              variant.identity.reset();
              variant.explicitAddress = *address;
              variant.name = base.name.empty() ? "Dynamic envelope" : base.name + " [dynamic envelope]";
              for (size_t region = 0; region < variant.regions.size(); ++region) {
                variant.regions[region].envelope = effectiveEnvelopes[region];
              }
              auto& destination = instrumentSets[baseRef->set].instruments;
              selectedRef = PreparedInstrumentRef{
                  .set = baseRef->set,
                  .instrument = static_cast<u32>(destination.size()),
              };
              destination.push_back(std::move(variant));
              variants.push_back(VariantRecord{
                  .base = *baseRef,
                  .envelopes = std::move(effectiveEnvelopes),
                  .variant = selectedRef,
              });
            } else {
              result.diagnostics.push_back(envelopeWarning(
                  "dynamic-envelope-addresses-exhausted",
                  "Could not allocate another portable bank/program address for a dynamic envelope variant",
                  &note->header));
            }
          }
        }
      }

      const InstrumentAddress selectedAddress = preparedInstrumentAddress(selectedRef, instrumentSets);
      if (selectedAddress != outputAddress) {
        loweredEvents.push_back(InstrumentPerformanceEvent{
            .header = note->header,
            .bank = selectedAddress.bank,
            .program = selectedAddress.program,
            .envelopeMode = InstrumentEnvelopeMode::PreserveDynamicOverride,
        });
        outputAddress = selectedAddress;
      }
      loweredEvents.push_back(event);
    }

    track.events = std::move(loweredEvents);
  }

  return result;
}

}  // namespace vgmtrans::core
