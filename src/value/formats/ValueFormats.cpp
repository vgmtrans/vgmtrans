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
#include "value/formats/ChunSnes/ChunSnes.h"
#include "value/formats/CompileSnes/CompileSnes.h"
#include "value/formats/CPS/Cps.h"
#include "value/formats/FalcomSnes/FalcomSnes.h"
#include "value/formats/HeartBeatSnes/HeartBeatSnes.h"
#include "value/formats/HudsonSnes/HudsonSnes.h"
#include "value/formats/ItikitiSnes/ItikitiSnes.h"
#include "value/formats/KonamiArcade/KonamiArcade.h"
#include "value/formats/KonamiSnes/KonamiSnes.h"
#include "value/formats/MP2k/MP2k.h"
#include "value/formats/NinSnes/NinSnes.h"
#include "value/formats/PrismSnes/PrismSnes.h"
#include "value/formats/RareSnes/RareSnes.h"
#include "value/formats/NDS/Nds.h"
#include "value/formats/SegSat/SegSat.h"
#include "value/formats/SonyPS1/SonyPS1.h"
#include "value/formats/SuzukiPS1/SuzukiPS1.h"
#include "value/formats/SuzukiSnes/SuzukiSnes.h"
#include "value/formats/WolfTeamSnes/WolfTeamSnes.h"
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
      session.registerExtractor(mame::mameRomSetExtractor(mame::RomDatabase::load(*mameDatabasePath)));
    } catch (const std::exception&) {
      if (options.mameRomDatabase) {
        throw;
      }
    }
  }
  session.registerExtractor(snes_rsn::snesRsnExtractor());
  session.registerExtractor(snes_spc::snesSpcExtractor());
  session.registerExtractor(psf::psfExtractor());
  session.registerFormat(capcom_snes::capcomSnesDefinition());
  session.registerFormat(chun_snes::definition());
  session.registerFormat(compile_snes::definition());
  session.registerFormat(cps::cpsDefinition());
  session.registerFormat(falcom_snes::definition());
  session.registerFormat(heartbeat_snes::definition());
  session.registerFormat(hudson_snes::definition());
  session.registerFormat(itikiti_snes::definition());
  session.registerFormat(nds::ndsDefinition());
  session.registerFormat(akao_snes::akaoSnesDefinition());
  session.registerFormat(akao::akaoDefinition());
  session.registerFormat(konami_arcade::konamiArcadeDefinition());
  session.registerFormat(konami_snes::konamiSnesDefinition());
  session.registerFormat(mp2k::mp2kDefinition());
  session.registerFormat(nin_snes::definition());
  session.registerFormat(prism_snes::definition());
  session.registerFormat(rare_snes::definition());
  session.registerFormat(segsat::segSatDefinition());
  session.registerFormat(sony_ps1::sonyPs1Definition());
  session.registerFormat(suzuki_ps1::suzukiPs1Definition());
  session.registerFormat(suzuki_snes::definition());
  session.registerFormat(wolf_team_snes::definition());
}

}  // namespace vgmtrans::formats
