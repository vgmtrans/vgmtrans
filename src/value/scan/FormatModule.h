/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"
#include "value/scan/ScanTypes.h"

#include <span>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

struct MatchContext {
  const SourceStore& sources;
  const SessionSnapshot& snapshot;
};

struct FormatModule {
  // Function table registered by one format. Session calls canScan() first, then scan(),
  // and later resolveCollections() after new assets and facts have been added.
  using CanScan = bool (*)(const SourceFile& source, std::span<const u8> bytes);
  using Scan = ScanResult (*)(const ScanInput& input);
  using ResolveCollections = std::vector<DesiredCollection> (*)(const MatchContext& context);

  std::string_view name;
  // canScan should be cheap and non-mutating; scan does the full parse once selected.
  CanScan canScan = nullptr;
  Scan scan = nullptr;
  // Defaults to name when empty. Set this when a resolver intentionally uses a
  // different key prefix for its collections.
  std::string_view collectionResolver;
  ResolveCollections resolveCollections = nullptr;
};

}  // namespace vgmtrans::core
