/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"

#include <iosfwd>
#include <span>
#include <string>
#include <string_view>

namespace vgmtrans::shell {

enum class ExportTarget { Collection, Sequence, SoundBank, Samples, Stitch };

[[nodiscard]] core::ExportKind parseExportKind(std::string_view text);
// Parse into the core's request directly; reject options the target cannot use.
[[nodiscard]] core::ExportRequest parseExportOptions(std::span<const std::string> args, ExportTarget target);
void printExportOptions(std::ostream& output);

}  // namespace vgmtrans::shell
