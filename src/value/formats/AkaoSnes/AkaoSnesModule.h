/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

namespace vgmtrans::core {
class FormatRegistry;
}

namespace vgmtrans::formats::akao_snes {

void registerAkaoSnesModule(core::FormatRegistry& registry);

}  // namespace vgmtrans::formats::akao_snes
