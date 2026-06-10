/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/SynthExportData.h"

#include "value/core/SampleDecoder.h"
#include "value/export/ExportDiagnostics.h"

#include <algorithm>
#include <limits>

namespace vgmtrans::core {

namespace {

[[nodiscard]] u16 clampU16(u32 value) {
  return static_cast<u16>(std::min<u32>(value, std::numeric_limits<u16>::max()));
}

}  // namespace

std::vector<DecodedSynthSample> decodeSynthSamples(
    std::span<const SampleCollectionAsset* const> sampleCollections,
    const SourceStore& sources,
    std::vector<Diagnostic>& diagnostics,
    const SynthSampleDecodeOptions& options) {
  std::vector<DecodedSynthSample> samples;
  auto decoders = SampleDecoderRegistry::withDefaultDecoders();

  for (const auto* collection : sampleCollections) {
    if (collection == nullptr) {
      continue;
    }

    for (u32 sampleIndex = 0; sampleIndex < collection->samples.samples.size(); ++sampleIndex) {
      const auto& sample = collection->samples.samples[sampleIndex];
      if (!sources.contains(sample.encodedData.source)) {
        diagnostics.push_back(exportError("Sample source was not found", validDiagnosticRange(sample.encodedData)));
        continue;
      }

      auto decoded = decoders.decode(sample, sources.bytes(sample.encodedData.source));
      if (!decoded) {
        diagnostics.push_back(
            exportError("No decoder registered for sample codec", validDiagnosticRange(sample.encodedData)));
        continue;
      }

      if (options.requireMono && decoded->channels != 1) {
        diagnostics.push_back(exportWarning(options.nonMonoWarning.empty() ? "Skipping non-mono sample for synth export"
                                                                          : options.nonMonoWarning,
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

SynthSampleIndexMap synthSampleIndexMap(std::span<const DecodedSynthSample> samples) {
  SynthSampleIndexMap indexes;
  for (u32 i = 0; i < samples.size(); ++i) {
    indexes[{samples[i].collectionId.value, samples[i].localIndex}] = clampU16(i);
  }
  return indexes;
}

std::optional<AssetId> firstSampleCollectionId(
    std::span<const SampleCollectionAsset* const> sampleCollections) {
  for (const auto* collection : sampleCollections) {
    if (collection != nullptr) {
      return collection->metadata.id;
    }
  }
  return std::nullopt;
}

std::optional<u16> resolveRegionSampleIndex(
    const Region& region,
    std::optional<AssetId> fallbackCollection,
    const SynthSampleIndexMap& samples,
    std::vector<Diagnostic>& diagnostics) {
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

std::vector<ResolvedSynthInstrument> resolveSynthInstruments(
    std::span<const InstrumentSetAsset* const> instrumentSets,
    std::span<const SampleCollectionAsset* const> sampleCollections,
    const SynthSampleIndexMap& samples,
    std::vector<Diagnostic>& diagnostics) {
  std::vector<ResolvedSynthInstrument> instruments;
  const auto fallbackCollection = firstSampleCollectionId(sampleCollections);

  for (const auto* instrumentSet : instrumentSets) {
    if (instrumentSet == nullptr) {
      continue;
    }

    for (const auto& instrument : instrumentSet->instruments) {
      ResolvedSynthInstrument resolvedInstrument{.instrument = &instrument};
      for (const auto& region : instrument.regions) {
        const auto sampleIndex = resolveRegionSampleIndex(region, fallbackCollection, samples, diagnostics);
        if (!sampleIndex) {
          continue;
        }

        resolvedInstrument.regions.push_back(ResolvedSynthRegion{
            .region = &region,
            .sampleIndex = *sampleIndex,
        });
      }

      if (!resolvedInstrument.regions.empty()) {
        instruments.push_back(std::move(resolvedInstrument));
      }
    }
  }

  return instruments;
}

}  // namespace vgmtrans::core
