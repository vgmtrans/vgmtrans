/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"
#include "value/sequence/SequenceDialect.h"

#include <vector>

namespace vgmtrans::core {

// One registration value supplies one ordered module and zero or more globally
// available sequence dialect implementations.
struct FormatDefinition {
  FormatModule module;
  std::vector<SequenceDialect> sequenceDialects;
};

}  // namespace vgmtrans::core
