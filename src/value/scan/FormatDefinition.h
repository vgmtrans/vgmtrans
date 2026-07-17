/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"
#include "value/sequence/SequenceDialect.h"

#include <optional>

namespace vgmtrans::core {

// One registration value owns a scanner and, when the format contains source
// bytecode, its executor family. Transitional formats may still use the two
// registries directly until they are migrated to a single semantic dialect.
struct FormatDefinition {
  FormatModule module;
  std::optional<SequenceDialect> sequenceDialect;
};

}  // namespace vgmtrans::core
