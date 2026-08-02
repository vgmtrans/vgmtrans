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
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace vgmtrans::core {

namespace {

using SynthSampleIndexKey = std::pair<u32, u32>;
using SynthSampleIndexMap = std::map<SynthSampleIndexKey, u16>;
using SynthSampleIndexList = std::vector<SynthSampleIndexKey>;
using SynthInstrumentList = std::vector<const Instrument*>;

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

void markSelectedInstrument(const InstrumentPerformanceEvent& selection, std::span<const Instrument* const> instruments,
                            SynthInstrumentList& used) {
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

[[nodiscard]] SynthInstrumentList selectInstruments(std::span<const InstrumentSetAsset* const> instrumentSets,
                                                    const PerformanceSequence* sequenceUsage) {
  SynthInstrumentList instruments;
  for (const auto* instrumentSet : instrumentSets) {
    if (instrumentSet == nullptr) {
      continue;
    }
    for (const auto& instrument : instrumentSet->instruments) {
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

[[nodiscard]] std::vector<DecodedSynthSample> decodeSynthSamples(
    std::span<const SampleCollectionAsset* const> sampleCollections, const SourceStore& sources,
    std::vector<Diagnostic>& diagnostics, const SynthSampleDecodeOptions& options,
    const SynthSampleIndexList* sampleFilter) {
  // Decode once into a flat vector. Container exporters then decide how to lay out that
  // PCM, but all of them share the same source-range diagnostics.
  std::vector<DecodedSynthSample> samples;

  for (const auto* collection : sampleCollections) {
    if (collection == nullptr) {
      continue;
    }

    for (u32 sampleIndex = 0; sampleIndex < collection->samples.samples.size(); ++sampleIndex) {
      const SynthSampleIndexKey sampleKey{collection->metadata.id.value, sampleIndex};
      if (sampleFilter != nullptr && std::ranges::find(*sampleFilter, sampleKey) == sampleFilter->end()) {
        continue;
      }
      const auto& sample = collection->samples.samples[sampleIndex];
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

      samples.push_back(DecodedSynthSample{
          .collectionId = collection->metadata.id,
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

[[nodiscard]] SynthSampleIndexMap synthSampleIndexMap(std::span<const DecodedSynthSample> samples) {
  // Region references use collection-local sample indexes. Export containers need a flat
  // sample table, so keep a map from original reference identity to flat index.
  SynthSampleIndexMap indexes;
  for (u32 i = 0; i < samples.size(); ++i) {
    indexes[{samples[i].collectionId.value, samples[i].localIndex}] = clampU16(i);
  }
  return indexes;
}

[[nodiscard]] std::optional<AssetId> firstSampleCollectionId(
    std::span<const SampleCollectionAsset* const> sampleCollections) {
  for (const auto* collection : sampleCollections) {
    if (collection != nullptr) {
      return collection->metadata.id;
    }
  }
  return std::nullopt;
}

[[nodiscard]] SynthSampleIndexList referencedSamples(std::span<const Instrument* const> instruments,
                                                     std::span<const SampleCollectionAsset* const> sampleCollections) {
  SynthSampleIndexList samples;
  const auto fallbackCollection = firstSampleCollectionId(sampleCollections);
  for (const auto* instrument : instruments) {
    for (const auto& region : instrument->regions) {
      const auto collection = region.sample.collection ? region.sample.collection : fallbackCollection;
      if (!collection) {
        continue;
      }
      const SynthSampleIndexKey sample{collection->value, region.sample.index};
      if (std::ranges::find(samples, sample) == samples.end()) {
        samples.push_back(sample);
      }
    }
  }
  return samples;
}

[[nodiscard]] std::optional<u16> resolveRegionSampleIndex(const Region& region,
                                                          std::optional<AssetId> fallbackCollection,
                                                          const SynthSampleIndexMap& samples,
                                                          std::vector<Diagnostic>& diagnostics) {
  // Older formats often imply "the first sample collection in the collection" rather than
  // storing an explicit collection id on every region.
  const std::optional<AssetId> collectionId = region.sample.collection ? region.sample.collection : fallbackCollection;
  if (!collectionId) {
    diagnostics.push_back(
        exportError("Region does not reference a sample collection", validDiagnosticRange(region.range)));
    return std::nullopt;
  }

  const auto found = samples.find({collectionId->value, region.sample.index});
  if (found == samples.end()) {
    diagnostics.push_back(exportError("Region sample reference was not found", validDiagnosticRange(region.range)));
    return std::nullopt;
  }

  return found->second;
}

[[nodiscard]] std::vector<ResolvedSynthInstrument> resolveSynthInstruments(
    std::span<const Instrument* const> selectedInstruments,
    std::span<const SampleCollectionAsset* const> sampleCollections, const SynthSampleIndexMap& samples,
    std::vector<Diagnostic>& diagnostics) {
  // Drop only regions whose samples cannot be resolved. The rest of the instrument can
  // still produce a useful partial export.
  std::vector<ResolvedSynthInstrument> instruments;
  const auto fallbackCollection = firstSampleCollectionId(sampleCollections);

  for (const auto* instrument : selectedInstruments) {
    auto modulation = lowerSynthModulation(instrument->modulation);
    ResolvedSynthInstrument resolvedInstrument{
        .instrument = instrument,
        .address = resolveInstrumentAddress(instrument->explicitAddress, instrument->identity),
        .generators = std::move(modulation.generators),
        .modulators = std::move(modulation.modulators),
    };
    for (const auto& region : instrument->regions) {
      const auto sampleIndex = resolveRegionSampleIndex(region, fallbackCollection, samples, diagnostics);
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

}  // namespace

Envelope approximateEnvelopeAsAdsr(Envelope envelope) {
  constexpr double endlessReleaseFallbackSeconds = 150.0;
  if (envelope.releaseSeconds && std::isinf(*envelope.releaseSeconds) && *envelope.releaseSeconds > 0.0) {
    // SF2 and DLS cannot represent an endless release, so use 150 seconds.
    envelope.releaseSeconds = endlessReleaseFallbackSeconds;
  }

  if (!envelope.secondDecaySeconds) {
    return envelope;
  }

  const double secondDecay = *envelope.secondDecaySeconds;
  const double sustain = envelope.sustainAmplitude.value_or(1.0);
  const double firstDecayDropDb = sustain > 0.0 ? -20.0 * std::log10(sustain) : std::numeric_limits<double>::infinity();
  constexpr double usefulDecayRangeDb = 70.0;
  constexpr double negligibleStageSeconds = 0.02;
  const double firstStageSeconds =
      envelope.decaySeconds && std::isfinite(*envelope.decaySeconds) && *envelope.decaySeconds >= 0.0
          ? *envelope.decaySeconds * std::clamp(firstDecayDropDb / usefulDecayRangeDb, 0.0, 1.0)
          : std::numeric_limits<double>::infinity();
  const bool negligibleFirstStage = firstDecayDropDb <= 1.0 && firstStageSeconds <= negligibleStageSeconds;
  const bool briefFirstStage = firstStageSeconds <= 0.05;
  constexpr double prominentDecayRateDbPerSecond = 7.0;
  const bool prominentSecondStage = std::isfinite(secondDecay) && secondDecay > 0.0 &&
                                    secondDecay <= usefulDecayRangeDb / prominentDecayRateDbPerSecond;
  const bool useSecondDecay = negligibleFirstStage || (briefFirstStage && prominentSecondStage);
  if (std::isfinite(secondDecay) && sustain >= 1.0) {
    envelope.decaySeconds = secondDecay;
    envelope.sustainAmplitude = 0.0;
  } else if (secondDecay >= 0.0 && secondDecay < 2.0) {
    // A short second decay sounds closer to one continuous fade than to a
    // permanent sustain level.
    envelope.decaySeconds = envelope.decaySeconds.value_or(0.0) + secondDecay;
    envelope.sustainAmplitude = 0.0;
  } else if (std::isfinite(secondDecay) && useSecondDecay) {
    // Keep the audible fade when the first stage is negligible, or when it is
    // only a brief lead-in to a much clearer decay.
    envelope.decaySeconds = secondDecay;
    envelope.sustainAmplitude = 0.0;
  }
  envelope.secondDecaySeconds.reset();
  return envelope;
}

PreparedSynthData prepareSynthData(const SynthExportInput& input, const SourceStore& sources,
                                   const SynthSampleDecodeOptions& options) {
  PreparedSynthData prepared;
  const auto instruments = selectInstruments(input.instrumentSets, input.sequenceUsage);
  std::optional<SynthSampleIndexList> sampleFilter;
  if (input.sequenceUsage != nullptr) {
    sampleFilter = referencedSamples(instruments, input.sampleCollections);
  }
  prepared.samples = decodeSynthSamples(input.sampleCollections, sources, prepared.diagnostics, options,
                                        sampleFilter ? &*sampleFilter : nullptr);
  const auto samplesByReference = synthSampleIndexMap(prepared.samples);
  prepared.instruments =
      resolveSynthInstruments(instruments, input.sampleCollections, samplesByReference, prepared.diagnostics);
  return prepared;
}

}  // namespace vgmtrans::core
