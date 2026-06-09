/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "core/Model.h"

#include <string>

namespace vgmtrans::formats::capcom_snes {

[[nodiscard]] std::string capcomSnesCommandDetailKind(const core::SequencerCommand& command);
[[nodiscard]] std::string capcomSnesCommandDescription(const core::SequencerCommand& command);

}  // namespace vgmtrans::formats::capcom_snes
