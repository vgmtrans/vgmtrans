/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"

#include <utility>

namespace vgmtrans::core::detail {

// Single internal entry point for projecting already-consistent session state
// into the public read-only model.
class SessionSnapshotAccess {
public:
  [[nodiscard]] static SessionSnapshot create(std::vector<SourceFile> sources, SharedSequence<Asset> assets,
                                              SharedSequence<MatchFact> matchFacts, std::vector<Collection> collections,
                                              SourceMap sourceMap, std::vector<Diagnostic> diagnostics) {
    return SessionSnapshot{
        std::move(sources),     std::move(assets),    std::move(matchFacts),
        std::move(collections), std::move(sourceMap), std::move(diagnostics),
    };
  }
};

}  // namespace vgmtrans::core::detail
