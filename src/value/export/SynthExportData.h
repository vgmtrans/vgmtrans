/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/Source.h"
#include "value/core/SynthModel.h"

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

// Shared decoded-sample view used by synth exporters before writing format-specific containers.
struct DecodedSynthSample {
  AssetId collectionId;
  u32 localIndex = 0;
  std::string name;
  Tuning pitch;
  double attenuationDb = 0.0;
  DecodedSample decoded;
};

using SynthSampleIndexKey = std::pair<u32, u32>;
using SynthSampleIndexMap = std::map<SynthSampleIndexKey, u16>;

// Decodes samples while preserving collection-local indexes used by instrument regions.
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

}  // namespace vgmtrans::core
