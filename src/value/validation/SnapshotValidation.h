/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SessionSnapshot.h"
#include "value/validation/ValidationReport.h"

#include <span>

namespace vgmtrans::core {

// validateSessionSnapshotState() is safe during snapshot construction, before
// indexes exist. validateSessionSnapshot() is a fuller audit for tests/debug tools.
[[nodiscard]] ValidationReport validateSessionSnapshotState(std::span<const Asset> assets,
                                                            std::span<const Collection> collections);
[[nodiscard]] ValidationReport validateSessionSnapshot(const SessionSnapshot& snapshot);

}  // namespace vgmtrans::core
