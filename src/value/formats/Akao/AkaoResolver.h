/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/scan/FormatModule.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace vgmtrans::formats::akao {

struct AkaoSampleCandidate {
  std::size_t index = 0;
  std::optional<u32> sampleSetId;
  u32 firstArt = 0;
  u32 artCount = 0;
  u32 scanOrdinal = 0;
};

[[nodiscard]] std::vector<std::size_t> selectAkaoSampleCandidates(std::optional<u32> sequenceSampleSet,
                                                                  std::span<const u32> requiredArticulations,
                                                                  std::span<const AkaoSampleCandidate> candidates);
[[nodiscard]] std::vector<core::DesiredCollection> resolveAkaoCollections(const core::MatchContext& context);

}  // namespace vgmtrans::formats::akao
