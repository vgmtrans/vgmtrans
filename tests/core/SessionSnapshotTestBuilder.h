/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshotAccess.h"

#include <utility>

namespace vgmtrans::core {

// Raw snapshot construction is useful for focused pure-consumer tests, but it
// deliberately bypasses Session admission and is not part of the production
// model. These fixtures do not define which states Session may publish.
class SessionSnapshotTestBuilder {
public:
  std::vector<SourceFile> sources;
  std::vector<Asset> assets;
  std::vector<MatchFact> matchFacts;
  std::vector<Collection> collections;
  SourceMap sourceMap;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] SessionSnapshot finish() {
    return detail::SessionSnapshotAccess::create(std::move(sources), std::move(assets), std::move(matchFacts),
                                                 std::move(collections), std::move(sourceMap), std::move(diagnostics));
  }
};

}  // namespace vgmtrans::core
