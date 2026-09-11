/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <filesystem>
#include <optional>

namespace vgmtrans::core {

class Session;

}  // namespace vgmtrans::core

namespace vgmtrans::formats {

struct ValueFormatOptions {
  // An explicit path is strict: registration throws when it cannot be loaded.
  // With no path, registration searches the installed/build resource locations
  // and simply omits MAME extraction when no database is available.
  std::optional<std::filesystem::path> mameRomDatabase;
};

void registerValueFormats(core::Session& session);
void registerValueFormats(core::Session& session, const ValueFormatOptions& options);

}  // namespace vgmtrans::formats
