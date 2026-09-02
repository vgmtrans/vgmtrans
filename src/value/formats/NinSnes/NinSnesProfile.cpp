/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnes.h"

#include <array>

namespace vgmtrans::formats::nin_snes {

namespace {

constexpr Profile kUnknown{
    .base = BaseProfile::Unknown,
    .playlist = PlaylistModel::Unknown,
    .programs = ProgramResolver::Direct,
};

constexpr std::array<Profile, 18> kProfiles{{
    {.id = ProfileId::Earlier,
     .name = "Earlier",
     .base = BaseProfile::Earlier,
     .instruments = InstrumentLayout::Earlier5Byte},
    {.id = ProfileId::Standard, .name = "Standard"},
    {.id = ProfileId::Rd1, .name = "RD1"},
    {.id = ProfileId::Rd2, .name = "RD2"},
    {.id = ProfileId::Hal, .name = "HAL", .pan = PanModel::HalTable},
    {.id = ProfileId::Konami,
     .name = "Konami",
     .addresses = AddressModel::KonamiBase,
     .instruments = InstrumentLayout::KonamiTuningTable,
     .tempoCommandMultiplier = 2},
    {.id = ProfileId::Lemmings, .name = "Lemmings", .noteParameters = NoteParameterModel::Lemmings},
    {.id = ProfileId::IntelliFe3,
     .name = "Intelligent Systems FE3",
     .base = BaseProfile::Intelli,
     .noteParameters = NoteParameterModel::IntelliTable,
     .programs = ProgramResolver::Direct,
     .intelli = IntelliMode::Fe3},
    {.id = ProfileId::IntelliTa,
     .name = "Intelligent Systems TA",
     .base = BaseProfile::Intelli,
     .programs = ProgramResolver::IntelliTaOverride,
     .intelli = IntelliMode::Ta},
    {.id = ProfileId::IntelliFe4,
     .name = "Intelligent Systems FE4",
     .base = BaseProfile::Intelli,
     .noteParameters = NoteParameterModel::IntelliTable,
     .programs = ProgramResolver::Direct,
     .intelli = IntelliMode::Fe4},
    {.id = ProfileId::Human,
     .name = "Human",
     .programs = ProgramResolver::Direct,
     .instrumentTable = InstrumentTableAddressModel::Human},
    {.id = ProfileId::Tose,
     .name = "TOSE",
     .playlist = PlaylistModel::Tose,
     .pan = PanModel::ToseLinear,
     .instrumentTable = InstrumentTableAddressModel::Tose},
    {.id = ProfileId::QuintetActR, .name = "Quintet ActRaiser", .programs = ProgramResolver::QuintetActRBase},
    {.id = ProfileId::QuintetActR2, .name = "Quintet ActRaiser 2", .programs = ProgramResolver::QuintetLookup},
    {.id = ProfileId::QuintetIog, .name = "Quintet Illusion of Gaia", .programs = ProgramResolver::QuintetLookup},
    {.id = ProfileId::QuintetTs, .name = "Quintet Terranigma", .programs = ProgramResolver::QuintetLookup},
    {.id = ProfileId::FalcomYs4, .name = "Falcom Ys IV", .addresses = AddressModel::FalcomBaseOffset},
    {.id = ProfileId::AirManagement, .name = "Air Management", .trackCount = 6},
}};

}  // namespace

const Profile& profile(ProfileId id) {
  for (const Profile& candidate : kProfiles) {
    if (candidate.id == id) {
      return candidate;
    }
  }
  return kUnknown;
}

u16 Layout::resolveAddress(u16 rawAddress) const {
  const Profile& selected = nin_snes::profile(profile);
  switch (selected.addresses) {
    case AddressModel::KonamiBase:
      return static_cast<u16>(konamiBaseAddress + rawAddress);
    case AddressModel::FalcomBaseOffset:
      return static_cast<u16>(falcomBaseOffset + rawAddress);
    case AddressModel::Direct:
    default:
      return rawAddress;
  }
}

u32 instrumentHeaderSize(const Profile& selected) {
  return selected.instruments == InstrumentLayout::Earlier5Byte ? 5 : 6;
}

u16 instrumentSlotCount(const Profile& selected) {
  if (selected.instrumentTable == InstrumentTableAddressModel::Human) {
    return static_cast<u16>((0x200 / instrumentHeaderSize(selected)) + 1);
  }
  return 0x80;
}

}  // namespace vgmtrans::formats::nin_snes
