/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnes.h"

#include <array>

namespace vgmtrans::formats::nin_snes {

namespace {

constexpr Profile kUnknown{};

constexpr std::array<Profile, 17> kProfiles{{
    {ProfileId::Earlier, "Earlier", BaseProfile::Earlier, AddressModel::Direct,
     PlaylistModel::Standard, NoteParameterModel::Standard, ProgramResolver::StandardPercussion,
     PanModel::StandardTable, InstrumentLayout::Earlier5Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::None},
    {ProfileId::Standard, "Standard", BaseProfile::Standard, AddressModel::Direct,
     PlaylistModel::Standard, NoteParameterModel::Standard, ProgramResolver::StandardPercussion,
     PanModel::StandardTable, InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::None},
    {ProfileId::Rd1, "RD1", BaseProfile::Standard, AddressModel::Direct, PlaylistModel::Standard,
     NoteParameterModel::Standard, ProgramResolver::StandardPercussion, PanModel::StandardTable,
     InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard, IntelliMode::None},
    {ProfileId::Rd2, "RD2", BaseProfile::Standard, AddressModel::Direct, PlaylistModel::Standard,
     NoteParameterModel::Standard, ProgramResolver::StandardPercussion, PanModel::StandardTable,
     InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard, IntelliMode::None},
    {ProfileId::Hal, "HAL", BaseProfile::Standard, AddressModel::Direct, PlaylistModel::Standard,
     NoteParameterModel::Standard, ProgramResolver::StandardPercussion, PanModel::HalTable,
     InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard, IntelliMode::None},
    {ProfileId::Konami, "Konami", BaseProfile::Standard, AddressModel::KonamiBase,
     PlaylistModel::Standard, NoteParameterModel::Standard, ProgramResolver::StandardPercussion,
     PanModel::StandardTable, InstrumentLayout::KonamiTuningTable,
     InstrumentTableAddressModel::Standard, IntelliMode::None},
    {ProfileId::Lemmings, "Lemmings", BaseProfile::Standard, AddressModel::Direct,
     PlaylistModel::Standard, NoteParameterModel::Lemmings, ProgramResolver::StandardPercussion,
     PanModel::StandardTable, InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::None},
    {ProfileId::IntelliFe3, "Intelligent Systems FE3", BaseProfile::Intelli, AddressModel::Direct,
     PlaylistModel::Standard, NoteParameterModel::IntelliTable, ProgramResolver::Direct,
     PanModel::StandardTable, InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::Fe3},
    {ProfileId::IntelliTa, "Intelligent Systems TA", BaseProfile::Intelli, AddressModel::Direct,
     PlaylistModel::Standard, NoteParameterModel::Standard, ProgramResolver::IntelliTaOverride,
     PanModel::StandardTable, InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::Ta},
    {ProfileId::IntelliFe4, "Intelligent Systems FE4", BaseProfile::Intelli, AddressModel::Direct,
     PlaylistModel::Standard, NoteParameterModel::IntelliTable, ProgramResolver::Direct,
     PanModel::StandardTable, InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::Fe4},
    {ProfileId::Human, "Human", BaseProfile::Standard, AddressModel::Direct, PlaylistModel::Standard,
     NoteParameterModel::Standard, ProgramResolver::Direct, PanModel::StandardTable,
     InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Human, IntelliMode::None},
    {ProfileId::Tose, "TOSE", BaseProfile::Standard, AddressModel::Direct, PlaylistModel::Tose,
     NoteParameterModel::Standard, ProgramResolver::StandardPercussion, PanModel::ToseLinear,
     InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Tose, IntelliMode::None},
    {ProfileId::QuintetActR, "Quintet ActRaiser", BaseProfile::Standard, AddressModel::Direct,
     PlaylistModel::Standard, NoteParameterModel::Standard, ProgramResolver::QuintetActRBase,
     PanModel::StandardTable, InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::None},
    {ProfileId::QuintetActR2, "Quintet ActRaiser 2", BaseProfile::Standard, AddressModel::Direct,
     PlaylistModel::Standard, NoteParameterModel::Standard, ProgramResolver::QuintetLookup,
     PanModel::StandardTable, InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::None},
    {ProfileId::QuintetIog, "Quintet Illusion of Gaia", BaseProfile::Standard, AddressModel::Direct,
     PlaylistModel::Standard, NoteParameterModel::Standard, ProgramResolver::QuintetLookup,
     PanModel::StandardTable, InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::None},
    {ProfileId::QuintetTs, "Quintet Terranigma", BaseProfile::Standard, AddressModel::Direct,
     PlaylistModel::Standard, NoteParameterModel::Standard, ProgramResolver::QuintetLookup,
     PanModel::StandardTable, InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::None},
    {ProfileId::FalcomYs4, "Falcom Ys IV", BaseProfile::Standard, AddressModel::FalcomBaseOffset,
     PlaylistModel::Standard, NoteParameterModel::Standard, ProgramResolver::StandardPercussion,
     PanModel::StandardTable, InstrumentLayout::Standard6Byte, InstrumentTableAddressModel::Standard,
     IntelliMode::None},
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

u16 convertAddress(const Profile& selected, u16 rawAddress, u16 konamiBaseAddress,
                   u16 falcomBaseOffset) {
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

u16 readAddress(const Profile& selected, core::ByteReader reader, u32 offset,
                u16 konamiBaseAddress, u16 falcomBaseOffset) {
  return convertAddress(selected, reader.le16(offset), konamiBaseAddress, falcomBaseOffset);
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
