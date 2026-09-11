/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/validation/ValidationReport.h"

namespace vgmtrans::core {

struct SequenceProgram;

// Checks source-neutral sequence structure. Source byte ranges are checked by scan validation.
[[nodiscard]] ValidationReport validateSequenceProgram(const SequenceProgram& program);

}  // namespace vgmtrans::core
