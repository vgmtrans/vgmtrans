/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/synth/SynthExportData.h"

#include "value/export/ExportDiagnostics.h"
#include "value/sequence/PerformanceModel.h"
#include "value/synth/SampleDecoder.h"

#include <algorithm>
#include <compare>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace vgmtrans::core {

namespace {

struct SynthSampleIndexKey {
  u32 owner = invalidIdValue;
  u32 index = invalidIdValue;
  bool phaseInverted = false;

  friend auto operator<=>(const SynthSampleIndexKey&, const SynthSampleIndexKey&) = default;
};

using SynthSampleIndexMap = std::map<SynthSampleIndexKey, u16>;
using SynthSampleIndexList = std::vector<SynthSampleIndexKey>;
using SynthInstrumentList = std::vector<const Instrument*>;

struct SamplePoolView {
  AssetId owner;
  const SamplePool* pool;
};

constexpr double kPerceivedHalfLoudnessDb = 10.0;

[[nodiscard]] u16 clampU16(u32 value) {
  return static_cast<u16>(std::min<u32>(value, std::numeric_limits<u16>::max()));
}

template <typename Predicate>
bool markMatchingInstruments(SynthInstrumentList& used, std::span<const Instrument* const> instruments,
                             Predicate matches) {
  bool found = false;
  for (const auto* instrument : instruments) {
    if (matches(*instrument)) {
      found = true;
      if (std::ranges::find(used, instrument) == used.end()) {
        used.push_back(instrument);
      }
    }
  }
  return found;
}

void markInstrumentAddress(InstrumentAddress address, std::span<const Instrument* const> instruments,
                           SynthInstrumentList& used) {
  markMatchingInstruments(used, instruments, [&](const Instrument& instrument) {
    return resolveInstrumentAddress(instrument.explicitAddress, instrument.identity) == address;
  });
}

void markSelectedInstrument(const InstrumentPerformanceEvent& selection,
                            std::span<const Instrument* const> instruments, SynthInstrumentList& used) {
  if (selection.sourceInstrument) {
    if (markMatchingInstruments(used, instruments, [&](const Instrument& instrument) {
          return instrument.identity && *instrument.identity == *selection.sourceInstrument;
        })) {
      return;
    }
    const auto fallbackAddress = resolveInstrumentAddress({}, selection.sourceInstrument);
    markMatchingInstruments(used, instruments, [&](const Instrument& instrument) {
      return resolveInstrumentAddress(instrument.explicitAddress, instrument.identity) == fallbackAddress;
    });
    return;
  }

  markInstrumentAddress(InstrumentAddress{.bank = selection.bank, .program = selection.program}, instruments, used);
}

[[nodiscard]] SynthInstrumentList selectInstruments(std::span<const SoundBankAsset* const> soundBanks,
                                                    const PerformanceSequence* sequenceUsage) {
  SynthInstrumentList instruments;
  for (const auto* soundBank : soundBanks) {
    if (soundBank == nullptr) {
      continue;
    }
    for (const auto& instrument : soundBank->instruments) {
      instruments.push_back(&instrument);
    }
  }
  if (sequenceUsage == nullptr) {
    return instruments;
  }

  SynthInstrumentList used;
  for (const auto& track : sequenceUsage->tracks) {
    // A track uses bank/program zero until its first instrument change.
    InstrumentPerformanceEvent selection;
    for (const auto& event : track.events) {
      if (const auto* change = std::get_if<InstrumentPerformanceEvent>(&event)) {
        selection = *change;
      } else if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
        if (note->instrumentAddress) {
          markInstrumentAddress(*note->instrumentAddress, instruments, used);
        } else {
          markSelectedInstrument(selection, instruments, used);
        }
      }
    }
  }
  std::erase_if(instruments,
                [&](const Instrument* instrument) { return std::ranges::find(used, instrument) == used.end(); });
  return instruments;
}

[[nodiscard]] std::vector<DecodedSynthSample> decodeSynthSamples(std::span<const SamplePoolView> samplePools,
                                                                 const SourceStore& sources,
                                                                 std::vector<Diagnostic>& diagnostics,
                                                                 const SynthSampleDecodeOptions& options,
                                                                 const SynthSampleIndexList* sampleFilter,
                                                                 SampleFilteringPolicy filtering) {
  // Decode once into a flat vector. Container exporters then decide how to lay out that
  // PCM, but all of them share the same source-range diagnostics.
  std::vector<DecodedSynthSample> samples;

  for (const auto& view : samplePools) {
    if (view.pool == nullptr) {
      continue;
    }
    const SampleFilter selectedFilter = resolveSampleFilter(filtering, view.pool->preferredFilter);

    for (u32 sampleIndex = 0; sampleIndex < view.pool->samples.size(); ++sampleIndex) {
      if (sampleFilter != nullptr && std::ranges::none_of(*sampleFilter, [&](const SynthSampleIndexKey& key) {
            return key.owner == view.owner.value && key.index == sampleIndex;
          })) {
        continue;
      }
      const auto& sample = view.pool->samples[sampleIndex];
      if (!sources.contains(sample.encodedData.source)) {
        diagnostics.push_back(exportError("Sample source was not found", validDiagnosticRange(sample.encodedData)));
        continue;
      }

      auto decoded = decodeSample(sample, sources.bytes(sample.encodedData.source));
      if (!decoded) {
        diagnostics.push_back(exportError("Unsupported sample codec", validDiagnosticRange(sample.encodedData)));
        continue;
      }

      if (options.requireMono && decoded->channels != 1) {
        diagnostics.push_back(exportWarning(
            options.nonMonoWarning.empty() ? "Skipping non-mono sample for synth export" : options.nonMonoWarning,
            validDiagnosticRange(sample.encodedData)));
        continue;
      }

      applySampleFilter(*decoded, selectedFilter);

      samples.push_back(DecodedSynthSample{
          .owner = view.owner,
          .localIndex = sampleIndex,
          .name = sample.name,
          .pitch = sample.pitch,
          .attenuationDb = sample.attenuationDb,
          .decoded = std::move(*decoded),
      });
    }
  }

  return samples;
}

[[nodiscard]] SynthSampleIndexList referencedSamples(std::span<const Instrument* const> instruments) {
  SynthSampleIndexList samples;
  for (const auto* instrument : instruments) {
    for (const auto& region : instrument->regions) {
      const SynthSampleIndexKey sample{
          region.sample.owner().value,
          region.sample.index(),
          region.invertSamplePhase,
      };
      if (std::ranges::find(samples, sample) == samples.end()) {
        samples.push_back(sample);
      }
    }
  }
  return samples;
}

[[nodiscard]] std::optional<u16> resolveRegionSampleIndex(const Region& region, const SynthSampleIndexMap& samples,
                                                          std::vector<Diagnostic>& diagnostics) {
  const auto found = samples.find({region.sample.owner().value, region.sample.index(), region.invertSamplePhase});
  if (found == samples.end()) {
    diagnostics.push_back(exportError("Region sample reference was not found", validDiagnosticRange(region.range)));
    return std::nullopt;
  }

  return found->second;
}

[[nodiscard]] SynthSampleIndexMap materializePhaseInvertedSamples(std::vector<DecodedSynthSample>& samples,
                                                                  const SynthSampleIndexList& references,
                                                                  bool discardUnreferenced) {
  SynthSampleIndexMap indexes;
  if (std::ranges::none_of(references, &SynthSampleIndexKey::phaseInverted)) {
    for (u32 i = 0; i < samples.size(); ++i) {
      indexes[{samples[i].owner.value, samples[i].localIndex, false}] = clampU16(i);
    }
    return indexes;
  }

  std::vector<DecodedSynthSample> materialized;
  materialized.reserve(samples.size() * 2);
  for (auto& sample : samples) {
    const auto referenced = [&](bool inverted) {
      return std::ranges::find(references, SynthSampleIndexKey{sample.owner.value, sample.localIndex, inverted}) !=
             references.end();
    };
    if (referenced(true)) {
      DecodedSynthSample inverted = sample;
      inverted.name += " [inverted]";
      for (s16& value : inverted.decoded.pcm) {
        value = value == std::numeric_limits<s16>::min() ? std::numeric_limits<s16>::max() : static_cast<s16>(-value);
      }
      indexes[{sample.owner.value, sample.localIndex, true}] = clampU16(static_cast<u32>(materialized.size()));
      materialized.push_back(std::move(inverted));
    }
    if (!discardUnreferenced || referenced(false)) {
      indexes[{sample.owner.value, sample.localIndex, false}] = clampU16(static_cast<u32>(materialized.size()));
      materialized.push_back(std::move(sample));
    }
  }
  samples = std::move(materialized);
  return indexes;
}

[[nodiscard]] std::vector<ResolvedSynthInstrument> resolveSynthInstruments(
    std::span<const Instrument* const> selectedInstruments, const SynthSampleIndexMap& samples,
    std::vector<Diagnostic>& diagnostics) {
  // Drop only regions whose samples cannot be resolved. The rest of the instrument can
  // still produce a useful partial export.
  std::vector<ResolvedSynthInstrument> instruments;
  for (const auto* instrument : selectedInstruments) {
    auto modulation = lowerSynthModulation(instrument->modulation);
    ResolvedSynthInstrument resolvedInstrument{
        .instrument = instrument,
        .address = resolveInstrumentAddress(instrument->explicitAddress, instrument->identity),
        .generators = std::move(modulation.generators),
        .modulators = std::move(modulation.modulators),
    };
    for (const auto& region : instrument->regions) {
      const auto sampleIndex = resolveRegionSampleIndex(region, samples, diagnostics);
      if (!sampleIndex) {
        continue;
      }

      auto regionModulation = lowerSynthModulation(region.modulation);
      resolvedInstrument.regions.push_back(ResolvedSynthRegion{
          .region = &region,
          .sampleIndex = *sampleIndex,
          .generators = std::move(regionModulation.generators),
          .modulators = std::move(regionModulation.modulators),
      });
    }

    if (!resolvedInstrument.regions.empty()) {
      instruments.push_back(std::move(resolvedInstrument));
    }
  }

  return instruments;
}

[[nodiscard]] double smoothstep(double low, double high, double value) {
  const double position = std::clamp((value - low) / (high - low), 0.0, 1.0);
  return position * position * (3.0 - 2.0 * position);
}

[[nodiscard]] double perceptualDecayFit(double firstDecay, double secondDecay, double firstDropDb,
                                        double attenuationRangeDb) {
  // In this model a 10 dB drop halves perceived loudness. Each source stage and
  // the target decay are therefore exponential curves in perceived loudness.
  // Minimize their squared difference over time; the closed-form integrals
  // make evaluating a candidate duration both cheap and deterministic.
  constexpr double ln2 = 0.6931471805599453;
  const double exponentScale = ln2 * attenuationRangeDb / kPerceivedHalfLoudnessDb;
  const double firstStageSeconds = firstDecay * firstDropDb / attenuationRangeDb;
  const double loudnessAtSecondStage = std::exp2(-firstDropDb / kPerceivedHalfLoudnessDb);
  const double firstExponent = firstDecay > 0.0 ? exponentScale / firstDecay : 0.0;
  const double secondExponent = secondDecay > 0.0 ? exponentScale / secondDecay : 0.0;

  const auto error = [&](double duration) {
    const double targetExponent = exponentScale / duration;
    double overlap = 0.0;
    if (firstStageSeconds > 0.0) {
      overlap +=
          (1.0 - std::exp(-(targetExponent + firstExponent) * firstStageSeconds)) / (targetExponent + firstExponent);
    }
    if (secondDecay > 0.0 && firstDropDb < attenuationRangeDb) {
      overlap +=
          std::exp(-targetExponent * firstStageSeconds) * loudnessAtSecondStage / (targetExponent + secondExponent);
    }
    // Source energy is constant with respect to the candidate duration, so it
    // can be omitted from the minimization.
    return 1.0 / (2.0 * targetExponent) - 2.0 * overlap;
  };

  if (firstDecay == 0.0 && secondDecay == 0.0) {
    return 0.0;
  }

  // There is only one best-fitting duration. Find it numerically instead of
  // relying on a complicated closed-form equation; 40 iterations are more
  // precise than the envelope timing that SF2 or DLS can store.
  constexpr double goldenRatio = 1.618033988749895;
  double low = 0.000001;
  double high = std::max(firstDecay, secondDecay) * 2.0;
  double left = high - (high - low) / goldenRatio;
  double right = low + (high - low) / goldenRatio;
  for (int iteration = 0; iteration < 40; ++iteration) {
    if (error(left) < error(right)) {
      high = right;
      right = left;
      left = high - (high - low) / goldenRatio;
    } else {
      low = left;
      left = right;
      right = low + (high - low) / goldenRatio;
    }
  }
  return (low + high) * 0.5;
}

}  // namespace

std::vector<const Instrument*> selectSynthInstruments(std::span<const SoundBankAsset* const> soundBanks,
                                                      const PerformanceSequence* sequenceUsage) {
  return selectInstruments(soundBanks, sequenceUsage);
}

Envelope approximateEnvelopeAsAdsr(Envelope envelope, double attenuationRangeDb) {
  constexpr double endlessReleaseFallbackSeconds = 150.0;
  if (envelope.releaseSeconds && std::isinf(*envelope.releaseSeconds) && *envelope.releaseSeconds > 0.0) {
    // SF2 and DLS cannot represent an endless release, so use 150 seconds.
    envelope.releaseSeconds = endlessReleaseFallbackSeconds;
  }

  if (!envelope.secondDecaySeconds) {
    return envelope;
  }

  const double secondDecay = *envelope.secondDecaySeconds;
  envelope.secondDecaySeconds.reset();
  if (!std::isfinite(secondDecay) || secondDecay < 0.0 || !std::isfinite(attenuationRangeDb) ||
      attenuationRangeDb <= 0.0) {
    return envelope;
  }

  const double sustain = std::clamp(envelope.sustainAmplitude.value_or(1.0), 0.0, 1.0);
  const double firstDropDb =
      sustain > 0.0 ? std::min(-20.0 * std::log10(sustain), attenuationRangeDb) : attenuationRangeDb;
  const double firstFraction = firstDropDb / attenuationRangeDb;
  if (firstFraction >= 1.0) {
    // The continuing stage starts at or below the target's silence floor.
    return envelope;
  }

  const double firstDecay = envelope.decaySeconds.value_or(0.0);
  if (firstFraction > 0.0 && (!std::isfinite(firstDecay) || firstDecay < 0.0)) {
    // An endless or invalid first stage cannot lead into a finite second one.
    return envelope;
  }

  if (firstFraction == 0.0) {
    envelope.decaySeconds = secondDecay;
    envelope.sustainAmplitude = 0.0;
    return envelope;
  }

  // Endpoint matching preserves the time to total silence, but can let a very
  // slow, quiet tail flatten an obviously separate first decay. Blend toward a
  // perceived-loudness-weighted fit when the first stage lasts long enough to
  // be heard independently. A first drop below 1% amplitude selects that fit
  // regardless of duration, since the following tail is already very quiet.
  const double endpointFit = firstDecay * firstFraction + secondDecay * (1.0 - firstFraction);
  const double perceptualFit = perceptualDecayFit(firstDecay, secondDecay, firstDropDb, attenuationRangeDb);
  const double firstStageSeconds = firstDecay * firstFraction;
  // Below 150 ms, the first stage tends to fuse with the onset. By 300 ms it
  // is heard as a separate fade and should fully outweigh a much quieter tail.
  const double firstStageSalience = smoothstep(0.15, 0.3, firstStageSeconds);
  // A rapid second stage remains perceptually important even when the first
  // stage is too brief to hear separately. Favor the loudness fit when that
  // stage halves perceived loudness within 150 ms, tapering to endpoint timing
  // by 300 ms. Slow tails retain their full duration.
  const double secondStageHalfLoudnessSeconds = secondDecay * kPerceivedHalfLoudnessDb / attenuationRangeDb;
  const double rapidSecondStageSalience = 1.0 - smoothstep(0.15, 0.3, secondStageHalfLoudnessSeconds);
  const double depthSalience = smoothstep(20.0, 40.0, firstDropDb);
  envelope.decaySeconds =
      std::lerp(endpointFit, perceptualFit, std::max({firstStageSalience, rapidSecondStageSalience, depthSalience}));
  envelope.sustainAmplitude = 0.0;
  return envelope;
}

PreparedSynthData prepareSynthData(const SynthExportInput& input, const SourceStore& sources,
                                   const SynthSampleDecodeOptions& options) {
  PreparedSynthData prepared;
  const auto instruments = selectInstruments(input.soundBanks, input.sequenceUsage);
  std::vector<SamplePoolView> samplePools;
  samplePools.reserve(input.soundBanks.size() + input.samplePools.size());
  for (const auto* bank : input.soundBanks) {
    if (bank != nullptr) {
      samplePools.push_back(SamplePoolView{.owner = bank->metadata.id, .pool = &bank->localSamples});
    }
  }
  for (const auto* pool : input.samplePools) {
    if (pool != nullptr) {
      samplePools.push_back(SamplePoolView{.owner = pool->metadata.id, .pool = &pool->pool});
    }
  }
  const SynthSampleIndexList sampleReferences = referencedSamples(instruments);
  const bool filterSamples = input.sequenceUsage != nullptr || input.filterSamplesToReferencedInstruments;
  prepared.samples = decodeSynthSamples(samplePools, sources, prepared.diagnostics, options,
                                        filterSamples ? &sampleReferences : nullptr, input.sampleFiltering);
  const auto samplesByReference =
      materializePhaseInvertedSamples(prepared.samples, sampleReferences, filterSamples);
  prepared.instruments = resolveSynthInstruments(instruments, samplesByReference, prepared.diagnostics);
  return prepared;
}

}  // namespace vgmtrans::core
