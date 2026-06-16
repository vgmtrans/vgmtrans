/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

namespace vgmtrans::core {

class SourceStore;
struct ScanCommit;

void validateScanCommitRanges(const ScanCommit& commit, const SourceStore& sources);

}  // namespace vgmtrans::core
