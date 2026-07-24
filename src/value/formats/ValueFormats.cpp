/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ValueFormats.h"

#include "value/extractors/MameRomSetExtractor.h"
#include "value/extractors/PsfExtractor.h"
#include "value/extractors/SnesRsnExtractor.h"
#include "value/extractors/SnesSpcExtractor.h"
#include "value/formats/Akao/Akao.h"
#include "value/formats/AkaoSnes/AkaoSnes.h"
#include "value/formats/CapcomSnes/CapcomSnes.h"
#include "value/formats/KonamiArcade/KonamiArcade.h"
#include "value/formats/KonamiSnes/KonamiSnes.h"
#include "value/formats/NDS/Nds.h"
#include "value/session/Session.h"

#include <array>
#include <exception>
#include <filesystem>

namespace vgmtrans::formats {

namespace {

[[nodiscard]] std::optional<std::filesystem::path> defaultMameRomDatabasePath() {
  std::error_code currentPathError;
  const auto currentPath = std::filesystem::current_path(currentPathError);
  std::array<std::filesystem::path, 5> candidates{
      currentPathError ? std::filesystem::path{} : currentPath / "mame_roms.json",
      currentPathError ? std::filesystem::path{} : currentPath / "bin" / "mame_roms.json",
      currentPathError ? std::filesystem::path{}
                       : (currentPath / ".." / "Resources" / "mame_roms.json").lexically_normal(),
#if defined(VGMTRANS_DATA_DIR)
      std::filesystem::path(VGMTRANS_DATA_DIR) / "mame_roms.json",
#else
      std::filesystem::path{},
#endif
#if defined(DEV_ENV_BUILD_TREE)
      std::filesystem::path(DEV_ENV_BUILD_TREE) / "mame_roms.json",
#else
      std::filesystem::path{},
#endif
  };

  for (const auto& candidate : candidates) {
    std::error_code error;
    if (!candidate.empty() && std::filesystem::is_regular_file(candidate, error) && !error) {
      return candidate;
    }
  }
  return std::nullopt;
}

}  // namespace

void registerValueFormats(core::Session& session) {
  registerValueFormats(session, ValueFormatOptions{});
}

void registerValueFormats(core::Session& session, const ValueFormatOptions& options) {
  const auto mameDatabasePath = options.mameRomDatabase ? options.mameRomDatabase : defaultMameRomDatabasePath();
  if (mameDatabasePath) {
    try {
      session.registerFormat(mame::mameRomSetExtractorDefinition(mame::RomDatabase::load(*mameDatabasePath)));
    } catch (const std::exception&) {
      if (options.mameRomDatabase) {
        throw;
      }
    }
  }
  session.registerFormat(snes_rsn::snesRsnExtractorDefinition());
  session.registerFormat(snes_spc::snesSpcExtractorDefinition());
  session.registerFormat(psf::psfExtractorDefinition());
  session.registerFormat(capcom_snes::capcomSnesDefinition());
  session.registerFormat(nds::ndsDefinition());
  session.registerFormat(akao_snes::akaoSnesDefinition());
  session.registerFormat(akao::akaoDefinition());
  session.registerFormat(konami_arcade::konamiArcadeDefinition());
  session.registerFormat(konami_snes::konamiSnesDefinition());
}

}  // namespace vgmtrans::formats
