/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/synth/SynthExportData.h"

#include "value/export/ExportDiagnostics.h"
#include "value/export/PerformanceInstrumentSelection.h"
#include "value/sequence/PerformanceModel.h"
#include "value/synth/SampleDecoder.h"

#include <fmt/format.h>

#include <algorithm>
#include <compare>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace vgmtrans::core {

namespace {

struct SynthSampleIndexKey {
  u32 owner = invalidIdValue;
  u32 index = invalidIdValue;
  bool phaseInverted = false;
  u32 startFrame = 0;

  friend auto operator<=>(const SynthSampleIndexKey&, const SynthSampleIndexKey&) = default;
};

// A requested sample variant has no output index until decoding succeeds.
using SynthSampleIndexMap = std::map<SynthSampleIndexKey, std::optional<u32>>;
using SynthInstrumentSet = std::set<const Instrument*>;

struct SamplePoolView {
  AssetId owner;
  const SamplePool& pool;
};

constexpr double kPerceivedHalfLoudnessDb = 10.0;

bool markMatchingInstruments(SynthInstrumentSet& used, std::span<const Instrument* const> instruments,
                             const InstrumentSelection& selection) {
  bool found = false;
  for (const auto* instrument : instruments) {
    if (matchesInstrumentSelection(*instrument, selection)) {
      found = true;
      used.insert(instrument);
    }
  }
  return found;
}

void markSelectedInstrument(const InstrumentSelection& selection, std::span<const Instrument* const> instruments,
                            SynthInstrumentSet& used) {
  if (!markMatchingInstruments(used, instruments, selection) && std::holds_alternative<InstrumentIdentity>(selection)) {
    markMatchingInstruments(used, instruments, resolveInstrumentAddress(selection));
  }
}

[[nodiscard]] SynthSampleIndexMap decodeSynthSamples(PreparedSynthData& prepared,
                                                     std::span<const SamplePoolView> samplePools,
                                                     const SourceStore& sources,
                                                     const SynthSampleDecodeOptions& options,
                                                     SynthSampleIndexMap indexes, bool discardUnreferenced,
                                                     SampleFilteringPolicy filtering) {
  // Decode once into the final sample table, including any phase-inverted
  // variants. Container exporters share its indexes and source diagnostics.
  for (const auto& view : samplePools) {
    const SampleFilter selectedFilter = resolveSampleFilter(filtering, view.pool.preferredFilter);

    for (u32 sampleIndex = 0; sampleIndex < view.pool.samples.size(); ++sampleIndex) {
      if (!discardUnreferenced) {
        indexes.try_emplace({view.owner.value, sampleIndex});
      }
      const auto first = indexes.lower_bound({view.owner.value, sampleIndex});
      const auto last = indexes.upper_bound({view.owner.value, sampleIndex, true, std::numeric_limits<u32>::max()});
      if (first == last) continue;
      const auto& sample = view.pool.samples[sampleIndex];
      if (!sources.contains(sample.encodedData.source)) {
        prepared.diagnostics.push_back(exportError("Sample source was not found", sample.encodedData));
        continue;
      }

      auto decoded = decodeSample(sample, sources.bytes(sample.encodedData.source));
      if (!decoded) {
        prepared.diagnostics.push_back(exportError("Unsupported sample codec", sample.encodedData));
        continue;
      }

      if (options.requireMono && decoded->channels != 1) {
        prepared.diagnostics.push_back(exportWarning(
            options.nonMonoWarning.empty() ? "Skipping non-mono sample for synth export" : options.nonMonoWarning,
            sample.encodedData));
        continue;
      }

      // S-DSP noise bypasses the BRR/Gaussian sample path, even when a sample
      // response filter is explicitly requested.
      if (sample.codec != AudioCodec::SnesDspNoise) {
        applySampleFilter(*decoded, selectedFilter);
      }

      // Visit inverted variants first, preserving the existing sample order.
      for (auto it = last; it != first;) {
        auto& [key, outputIndex] = *--it;
        auto audio = it == first ? std::move(*decoded) : *decoded;
        if (key.startFrame != 0) {
          const u64 skip = static_cast<u64>(key.startFrame) * audio.channels;
          if (skip >= audio.pcm.size()) {
            prepared.diagnostics.push_back(exportError("Sample start frame is outside decoded sample data"));
            continue;
          }
          // Trim after decoding/filtering to preserve ADPCM predictor history.
          audio.pcm.erase(audio.pcm.begin(), audio.pcm.begin() + skip);
          const u32 loopEnd = audio.loop.start + audio.loop.length;
          audio.loop.start -= std::min(audio.loop.start, key.startFrame);
          audio.loop.length = loopEnd - std::min(loopEnd, key.startFrame) - audio.loop.start;
          audio.loop.enabled &= audio.loop.length != 0;
        }
        if (key.phaseInverted) {
          for (s16& value : audio.pcm) {
            value = value == std::numeric_limits<s16>::min() ? std::numeric_limits<s16>::max() : static_cast<s16>(-value);
          }
        }
        outputIndex = static_cast<u32>(prepared.samples.size());
        prepared.samples.push_back(DecodedSynthSample{
            .name = sample.name + (key.startFrame ? " [sustain]" : "") + (key.phaseInverted ? " [inverted]" : ""),
            .pitch = sample.pitch,
            .attenuationDb = sample.attenuationDb,
            .decoded = std::move(audio),
        });
      }
    }
  }

  return indexes;
}

[[nodiscard]] SynthSampleIndexMap referencedSamples(std::span<const Instrument* const> instruments) {
  SynthSampleIndexMap samples;
  for (const auto* instrument : instruments) {
    for (const auto& region : instrument->regions) {
      samples.try_emplace(SynthSampleIndexKey{region.sample.owner().value, region.sample.index(),
                                              region.invertSamplePhase, region.sampleStartFrame});
    }
  }
  return samples;
}

[[nodiscard]] std::vector<ResolvedSynthInstrument> resolveSynthInstruments(
    std::span<const Instrument* const> selectedInstruments, const SynthSampleIndexMap& samples,
    const SynthExportInput& input, std::vector<Diagnostic>& diagnostics) {
  const auto lowerModulation = [&](const InstrumentModulation& modulation) {
    auto lowered = lowerSynthModulation(modulation, input.modulationConversion);
    for (auto& modulator : lowered.modulators) {
      modulator.amount = scaledSynthModulatorAmount(modulator, input.midiModulationUsage, input.modulationScaling);
    }
    return lowered;
  };
  // Drop only regions whose samples cannot be resolved. The rest of the instrument can
  // still produce a useful partial export.
  std::vector<ResolvedSynthInstrument> instruments;
  const SynthInstrumentSet selected{selectedInstruments.begin(), selectedInstruments.end()};
  for (const auto* bank : input.soundBanks) {
    if (!bank) {
      continue;
    }
    const u32 step = regionSamplingStep(*bank, diagnostics);
    for (const auto& instrument : bank->instruments) {
      if (!selected.contains(&instrument)) {
        continue;
      }
      ResolvedSynthInstrument resolvedInstrument{
          .instrument = &instrument,
          .address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity),
          .modulation = lowerModulation(instrument.modulation),
      };
      for (auto& region : sampleRegionResponses(instrument.regions, step)) {
        const auto sample = samples.at(
            {region.sample.owner().value, region.sample.index(), region.invertSamplePhase, region.sampleStartFrame});
        if (!sample) {
          diagnostics.push_back(exportError("Region sample reference was not found", region.range));
          continue;
        }

        auto modulation = lowerModulation(region.modulation);
        resolvedInstrument.regions.push_back(ResolvedSynthRegion{
            .region = std::move(region),
            .sampleIndex = *sample,
            .modulation = std::move(modulation),
        });
      }

      if (!resolvedInstrument.regions.empty()) {
        instruments.push_back(std::move(resolvedInstrument));
      }
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
  std::vector<const Instrument*> instruments;
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

  SynthInstrumentSet used;
  for (const auto& track : sequenceUsage->tracks) {
    const auto continuedNotes = performanceNotePredecessors(track);
    // A track uses bank/program zero until its first instrument change.
    InstrumentSelection selection;
    bool hasVoice = false;
    for (const auto& event : track.events) {
      if (const auto* change = std::get_if<InstrumentPerformanceEvent>(&event)) {
        selection = change->instrument;
      } else if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
        if (hasVoice && (note->extendsPrevious || continuedNotes.contains(note->note))) {
          continue;
        }
        hasVoice = true;
        if (note->instrumentAddress) {
          markSelectedInstrument(*note->instrumentAddress, instruments, used);
        } else {
          markSelectedInstrument(selection, instruments, used);
        }
      }
    }
  }
  std::erase_if(instruments, [&](const Instrument* instrument) { return !used.contains(instrument); });
  return instruments;
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

u32 regionSamplingStep(const SoundBankAsset& bank, std::vector<Diagnostic>& diagnostics, u32 maxRegions) {
  bool hasResponse = false;
  const auto countAt = [&](u32 step) {
    u64 count = 0;
    for (const auto& instrument : bank.instruments) {
      for (const auto& region : instrument.regions) {
        const auto& response = region.response;
        hasResponse |= bool(response.evaluate);
        if (response.evaluate &&
            (region.keyRange.low > region.keyRange.high || region.velocityRange.low > region.velocityRange.high)) {
          continue;
        }
        const u32 keys =
            response.evaluate && response.keyDependent ? region.keyRange.high - region.keyRange.low + 1 : 1;
        const u32 velocities = response.evaluate && response.velocityDependent
                                   ? region.velocityRange.high - region.velocityRange.low + 1
                                   : 1;
        count += ((keys + step - 1) / step) * ((velocities + step - 1) / step);
      }
    }
    return count;
  };
  const u64 exact = countAt(1);
  if (!hasResponse) {
    return 1;
  }
  u32 step = 1;
  while (step < 128 && countAt(step) > maxRegions) {
    ++step;
  }
  if (step != 1) {
    const u64 sampled = countAt(step);
    diagnostics.push_back(exportWarning(
        fmt::format("Key/velocity response requires {} exact regions; using {}-step zones ({} regions), {}", exact,
                    step, sampled,
                    sampled <= maxRegions ? "to fit synth tables"
                                          : "but the coarsest zones still exceed the conservative synth-table budget"),
        bank.metadata.range));
  }
  return step;
}

std::vector<Region> sampleRegionResponses(std::span<const Region> regions, u32 step) {
  step = std::clamp<u32>(step, 1, 128);
  std::vector<Region> sampled;
  sampled.reserve(regions.size());
  for (const auto& source : regions) {
    if (!source.response.evaluate) {
      sampled.push_back(source);
      continue;
    }
    Region base = source;
    const auto response = std::exchange(base.response, {});
    for (int key = base.keyRange.low; key <= base.keyRange.high;) {
      const int keyEnd = response.keyDependent ? std::min<int>(key + step - 1, base.keyRange.high) : base.keyRange.high;
      for (int velocity = base.velocityRange.low; velocity <= base.velocityRange.high;) {
        const int velocityEnd = response.velocityDependent ? std::min<int>(velocity + step - 1, base.velocityRange.high)
                                                           : base.velocityRange.high;
        Region region = base;
        region.keyRange = {static_cast<u8>(key), static_cast<u8>(keyEnd)};
        region.velocityRange = {static_cast<u8>(velocity), static_cast<u8>(velocityEnd)};
        response.evaluate(region, key + (keyEnd - key) / 2, velocity + (velocityEnd - velocity) / 2);
        sampled.push_back(std::move(region));
        velocity = velocityEnd + 1;
      }
      key = keyEnd + 1;
    }
  }
  return sampled;
}

PreparedSynthData prepareSynthData(const SynthExportInput& input, const SourceStore& sources,
                                   const SynthSampleDecodeOptions& options) {
  PreparedSynthData prepared;
  const auto instruments = selectSynthInstruments(input.soundBanks, input.sequenceUsage);
  std::vector<SamplePoolView> samplePools;
  samplePools.reserve(input.soundBanks.size() + input.samplePools.size());
  for (const auto* bank : input.soundBanks) {
    if (bank != nullptr) {
      samplePools.push_back(SamplePoolView{.owner = bank->metadata.id, .pool = bank->localSamples});
    }
  }
  for (const auto* pool : input.samplePools) {
    if (pool != nullptr) {
      samplePools.push_back(SamplePoolView{.owner = pool->metadata.id, .pool = pool->pool});
    }
  }
  const bool filterSamples = input.sequenceUsage != nullptr || input.filterSamplesToReferencedInstruments;
  const auto samplesByReference = decodeSynthSamples(
      prepared, samplePools, sources, options, referencedSamples(instruments), filterSamples, input.sampleFiltering);
  prepared.instruments = resolveSynthInstruments(instruments, samplesByReference, input, prepared.diagnostics);
  return prepared;
}

}  // namespace vgmtrans::core
