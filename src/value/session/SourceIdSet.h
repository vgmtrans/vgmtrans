/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <span>
#include <unordered_set>

namespace vgmtrans::core {

using SourceIdSet = std::unordered_set<SourceId>;

[[nodiscard]] inline SourceIdSet makeSourceIdSet(std::span<const SourceId> sources) {
  SourceIdSet ids;
  ids.reserve(sources.size());
  for (const SourceId source : sources) {
    ids.insert(source);
  }
  return ids;
}

}  // namespace vgmtrans::core
