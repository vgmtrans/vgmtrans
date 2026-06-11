/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/FormatRegistry.h"
#include "value/core/ProjectModel.h"
#include "value/core/Source.h"

namespace vgmtrans::core {

[[nodiscard]] Project scanProject(SourceStore& sources, const FormatRegistry& formats);

}  // namespace vgmtrans::core
