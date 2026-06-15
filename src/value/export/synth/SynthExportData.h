/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/synth/SynthModel.h"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::core {

struct SynthSampleDecodeOptions {
  bool requireMono = false;
  std::string nonMonoWarning;
};

// Decoded sample plus its original collection/index identity. Synth exporters use
// this to build one flat sample table without losing region references.
struct DecodedSynthSample {
  AssetId collectionId;
  u32 localIndex = 0;
  std::string name;
  Tuning pitch;
  double attenuationDb = 0.0;
  DecodedSample decoded;
};

struct ResolvedSynthRegion {
  const Region* region = nullptr;
  u16 sampleIndex = 0;
};

struct ResolvedSynthInstrument {
  const Instrument* instrument = nullptr;
  std::vector<ResolvedSynthRegion> regions;
};

using SynthSampleIndexKey = std::pair<u32, u32>;
using SynthSampleIndexMap = std::map<SynthSampleIndexKey, u16>;

// Decode all referenced sample collections once before writing a synth container.
// The returned samples still remember the collection-local indexes used by regions.
[[nodiscard]] std::vector<DecodedSynthSample> decodeSynthSamples(
    std::span<const SampleCollectionAsset* const> sampleCollections,
    const SourceStore& sources,
    std::vector<Diagnostic>& diagnostics,
    const SynthSampleDecodeOptions& options = {});

[[nodiscard]] SynthSampleIndexMap synthSampleIndexMap(std::span<const DecodedSynthSample> samples);

[[nodiscard]] std::optional<AssetId> firstSampleCollectionId(
    std::span<const SampleCollectionAsset* const> sampleCollections);

[[nodiscard]] std::optional<u16> resolveRegionSampleIndex(
    const Region& region,
    std::optional<AssetId> fallbackCollection,
    const SynthSampleIndexMap& samples,
    std::vector<Diagnostic>& diagnostics);

[[nodiscard]] std::vector<ResolvedSynthInstrument> resolveSynthInstruments(
    std::span<const InstrumentSetAsset* const> instrumentSets,
    std::span<const SampleCollectionAsset* const> sampleCollections,
    const SynthSampleIndexMap& samples,
    std::vector<Diagnostic>& diagnostics);

}  // namespace vgmtrans::core
