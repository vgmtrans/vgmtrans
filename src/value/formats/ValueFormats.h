/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

namespace vgmtrans::core {

class Session;

}  // namespace vgmtrans::core

namespace vgmtrans::formats {

void registerValueFormats(core::Session& session);

}  // namespace vgmtrans::formats
