/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshotAccess.h"

#include <utility>

namespace vgmtrans::core::test {

// Raw snapshot construction deliberately bypasses Session admission. Keep it
// explicit in the few pure-consumer tests that need otherwise-unpublishable state.
class SessionSnapshotBuilder {
public:
  std::vector<SourceFile> sources;
  std::vector<Asset> assets;
  std::vector<Collection> collections;
  SourceMap sourceMap;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] SessionSnapshot finish() {
    return detail::SessionSnapshotAccess::create(std::move(sources), SharedSequence<Asset>{std::move(assets)},
                                                 std::move(collections), std::move(sourceMap), std::move(diagnostics));
  }
};

}  // namespace vgmtrans::core::test
