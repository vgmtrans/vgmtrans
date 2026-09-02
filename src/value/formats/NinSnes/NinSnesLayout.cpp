/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnes.h"

#include "value/formats/NinSnes/KoeiSnesDriver.h"
#include "value/formats/NinSnes/NinSnesPatterns.h"

#include <algorithm>
#include <array>
#include <optional>

namespace vgmtrans::formats::nin_snes {

using namespace core;

namespace {

struct SongListInfo {
  u32 address = 0;
  Signature signature = Signature::None;
  ProfileId profile = ProfileId::Unknown;
  std::optional<u16> konamiBaseAddress;
  std::optional<u16> falcomBaseAddress;
};

struct VoiceCommandInfo {
  u8 first = 0;
  u16 addressTable = 0;
  u16 lengthTable = 0;
  u8 count = 0;
  Signature signature = Signature::None;
  ProfileId profile = ProfileId::Unknown;
};

struct InstrumentProbe {
  u32 tableAddress = 0;
  u16 dirAddress = 0;
};

constexpr std::array<u8, 27> kStandardCommandLengths{
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01,
    0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01,
};

constexpr std::array<u8, 40> kIntelliFe3CommandLengths{
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01,
    0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x02, 0x02,
};

constexpr std::array<u8, 36> kIntelliFe4CommandLengths{
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01, 0x02, 0x03, 0x01, 0x03,
    0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01, 0x00, 0x00, 0x02, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01,
};

constexpr std::array<u8, 36> kIntelliTaCommandLengths{
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01, 0x02, 0x03, 0x01, 0x03,
    0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01, 0x00, 0x00, 0x02, 0x02, 0x00, 0x01, 0x01, 0x01, 0x01,
};

template <size_t Size>
[[nodiscard]] bool matchesTable(ByteReader reader, u16 address, const std::array<u8, Size>& table) {
  return reader.has(address, table.size()) && std::ranges::equal(reader.slice(address, table.size()), table);
}

[[nodiscard]] std::vector<u8> readTable(ByteReader reader, u16 address, u8 length) {
  if (!reader.has(address, length)) {
    return {};
  }
  const auto bytes = reader.slice(address, length);
  return {bytes.begin(), bytes.end()};
}

[[nodiscard]] u8 commandCount(u8 first, u16 addresses, u16 lengths) {
  if (addresses < lengths && addresses + (0x100 - first) * 2 >= lengths) {
    return static_cast<u8>((lengths - addresses) / 2);
  }
  return 0;
}

[[nodiscard]] std::optional<SongListInfo> findSongList(ByteReader reader, u8 sectionPointer,
                                                       std::optional<u16> konamiBase) {
  const Pattern standard = Patterns::makeInitSectionPtrPattern(sectionPointer);
  const Pattern yi = Patterns::makeInitSectionPtrYIPattern(sectionPointer);
  const Pattern smw = Patterns::makeInitSectionPtrSMWPattern(sectionPointer);
  const Pattern gd3 = Patterns::makeInitSectionPtrGD3Pattern(sectionPointer);
  const Pattern ysfr = Patterns::makeInitSectionPtrYSFRPattern(sectionPointer);
  const Pattern ts = Patterns::makeInitSectionPtrTSPattern(sectionPointer);
  const Pattern ys4 = Patterns::makeInitSectionPtrYs4Pattern(sectionPointer);

  if (auto offset = standard.find(reader)) {
    if (const auto ys4Offset = ys4.find(reader)) {
      return SongListInfo{
          .address = reader.le16(*ys4Offset + 5),
          .signature = Signature::FalcomYs4,
          .profile = ProfileId::FalcomYs4,
          .falcomBaseAddress = static_cast<u16>(reader.u8At(*ys4Offset + 13) << 8),
      };
    }
    return SongListInfo{.address = reader.le16(*offset + 5)};
  }
  if (const auto offset = yi.find(reader)) {
    return SongListInfo{.address = reader.le16(*offset + 12)};
  }
  if (const auto offset = smw.find(reader)) {
    return SongListInfo{
        .address = reader.le16(*offset + 3),
        .signature = Signature::Earlier,
    };
  }
  if (const auto offset = gd3.find(reader)) {
    return SongListInfo{
        .address = reader.le16(*offset + 8),
        // Parodius omits the base-address addition entirely.
        .konamiBaseAddress = konamiBase.value_or(0),
    };
  }
  if (const auto offset = ts.find(reader)) {
    const u16 pointer = reader.le16(*offset + 1);
    if (!reader.has(pointer, 2)) {
      return std::nullopt;
    }
    return SongListInfo{
        .address = reader.le16(pointer),
        .signature = Signature::Quintet,
    };
  }
  if (const auto offset = Patterns::ptnInitSectionPtrHE4.find(reader)) {
    return SongListInfo{.address = static_cast<u32>(reader.u8At(*offset + 4) << 8)};
  }
  if (const auto offset = ysfr.find(reader)) {
    const u8 songListPointer = reader.u8At(*offset + 2);
    const Pattern initializeSongList = Patterns::makeInitSongListPtrYSFRPattern(songListPointer);
    const auto initializeOffset = initializeSongList.find(reader);
    if (!initializeOffset) {
      return std::nullopt;
    }
    return SongListInfo{
        .address = static_cast<u32>(reader.u8At(*initializeOffset + 1) | (reader.u8At(*initializeOffset + 4) << 8)),
    };
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<VoiceCommandInfo> findVoiceCommands(ByteReader reader) {
  VoiceCommandInfo info;

  if (const auto dispatch = Patterns::ptnJumpToVcmdYSFR.find(reader)) {
    info.addressTable = reader.le16(*dispatch + 9);
    const auto lengths = Patterns::ptnReadVcmdLengthYSFR.find(reader);
    if (!lengths) {
      return std::nullopt;
    }
    info.first = reader.u8At(*lengths + 2);
    info.lengthTable = reader.le16(*lengths + 7);
    info.profile = info.first == 0xe0 ? ProfileId::Tose : ProfileId::Unknown;
    info.count = commandCount(info.first, info.addressTable, info.lengthTable);
    return info;
  }

  if (const auto dispatch = Patterns::ptnJumpToVcmdYs4.find(reader)) {
    info.addressTable = reader.le16(*dispatch + 10);
    const auto lengths = Patterns::ptnReadVcmdLengthYs4.find(reader);
    if (!lengths) {
      return std::nullopt;
    }
    info.first = reader.u8At(*lengths + 1);
    info.lengthTable = reader.le16(*lengths + 9) + info.first;
    info.count = commandCount(info.first, info.addressTable, info.lengthTable);
    return info;
  }

  if (const auto readaheadBranch = Patterns::ptnBranchForVcmdReadahead.find(reader)) {
    info.first = reader.u8At(*readaheadBranch + 5);
  } else if (const auto normalBranch = Patterns::ptnBranchForVcmd.find(reader)) {
    // This broad signature remains necessary for Human Entertainment drivers.
    info.first = reader.u8At(*normalBranch + 1);
  } else {
    return std::nullopt;
  }

  if (const auto standardDispatch = Patterns::ptnJumpToVcmd.find(reader)) {
    if (const auto human = Patterns::ptnJumpToVcmdCTOW.find(reader)) {
      info.signature = Signature::Human;
      info.profile = ProfileId::Human;
      info.addressTable = reader.le16(*human + 10);
      info.lengthTable = reader.le16(*human + 17);
    } else {
      info.profile = ProfileId::Standard;
      info.addressTable = reader.le16(*standardDispatch + 7) + ((info.first * 2) & 0xff);
      info.lengthTable = reader.le16(*standardDispatch + 14) + (info.first & 0x7f);
    }
  } else if (const auto earlierDispatch = Patterns::ptnJumpToVcmdSMW.find(reader)) {
    const auto lengths = Patterns::ptnReadVcmdLengthSMW.find(reader);
    if (!lengths) {
      return std::nullopt;
    }
    info.profile = ProfileId::Earlier;
    info.addressTable = reader.le16(*earlierDispatch + 5) + ((info.first * 2) & 0xff);
    info.lengthTable = reader.le16(*lengths + 9) + info.first;
  } else {
    return std::nullopt;
  }

  info.count = commandCount(info.first, info.addressTable, info.lengthTable);
  return info;
}

[[nodiscard]] ProfileId classifyIntelligent(ByteReader reader, const VoiceCommandInfo& commands) {
  if (!Patterns::ptnIntelliVCmdFA.find(reader)) {
    return ProfileId::Unknown;
  }
  if (Patterns::ptnDispatchNoteFE3.find(reader)) {
    return commands.first == 0xd6 && matchesTable(reader, commands.lengthTable, kIntelliFe3CommandLengths)
               ? ProfileId::IntelliFe3
               : ProfileId::Unknown;
  }
  if (Patterns::ptnDispatchNoteFE4.find(reader)) {
    return commands.first == 0xda && matchesTable(reader, commands.lengthTable, kIntelliFe4CommandLengths)
               ? ProfileId::IntelliFe4
               : ProfileId::Unknown;
  }
  return commands.first == 0xda && matchesTable(reader, commands.lengthTable, kIntelliTaCommandLengths)
             ? ProfileId::IntelliTa
             : ProfileId::Unknown;
}

[[nodiscard]] ProfileId classifyStandard(ByteReader reader, const VoiceCommandInfo& commands,
                                         std::optional<u16> konamiBase, u32& instrumentCommandOffset) {
  if (konamiBase) {
    return ProfileId::Konami;
  }
  if (Patterns::ptnDispatchNoteLEM.find(reader)) {
    return commands.first == 0xe0 ? ProfileId::Lemmings : ProfileId::Unknown;
  }
  if (commands.first != 0xe0 || !matchesTable(reader, commands.lengthTable, kStandardCommandLengths)) {
    return classifyIntelligent(reader, commands);
  }

  const bool canonical = commands.addressTable + (kStandardCommandLengths.size() * 2) == commands.lengthTable;
  if (canonical) {
    if (Patterns::ptnWriteVolumeKSS.find(reader)) {
      return ProfileId::Hal;
    }
    if (const auto offset = Patterns::ptnInstrVCmdACTR.find(reader)) {
      instrumentCommandOffset = *offset;
      return ProfileId::QuintetActR;
    }
    if (const auto offset = Patterns::ptnInstrVCmdACTR2.find(reader)) {
      instrumentCommandOffset = *offset;
      return ProfileId::QuintetActR2;
    }
    return ProfileId::Standard;
  }

  const bool quintetTail =
      commands.count == 32 && reader.has(commands.lengthTable + 31, 1) && reader.u8At(commands.lengthTable + 31) == 1;
  if (Patterns::ptnRD1VCmd_FA_FE.find(reader)) {
    return ProfileId::Rd1;
  }
  if (Patterns::ptnRD2VCmdInstrADSR.find(reader)) {
    return ProfileId::Rd2;
  }
  if (quintetTail) {
    if (const auto offset = Patterns::ptnInstrVCmdACTR2.find(reader)) {
      instrumentCommandOffset = *offset;
      return ProfileId::QuintetIog;
    }
    if (const auto offset = Patterns::ptnInstrVCmdTS.find(reader)) {
      instrumentCommandOffset = *offset;
      return ProfileId::QuintetTs;
    }
  }
  return ProfileId::Standard;
}

[[nodiscard]] std::optional<u16> findDirAddress(ByteReader reader) {
  if (const auto offset = Patterns::ptnSetDIR.find(reader)) {
    return static_cast<u16>(reader.u8At(*offset + 4) << 8);
  }
  if (const auto offset = Patterns::ptnSetDIRYI.find(reader)) {
    return static_cast<u16>(reader.u8At(*offset + 1) << 8);
  }
  if (const auto offset = Patterns::ptnSetDIRVS.find(reader)) {
    const u16 pointer = reader.le16(*offset + 1);
    return reader.has(pointer, 1) ? std::optional<u16>{static_cast<u16>(reader.u8At(pointer) << 8)} : std::nullopt;
  }
  if (const auto offset = Patterns::ptnSetDIRSMW.find(reader)) {
    return static_cast<u16>(reader.u8At(*offset + 9) << 8);
  }
  if (const auto offset = Patterns::ptnSetDIRCTOW.find(reader)) {
    return static_cast<u16>(reader.u8At(*offset + 3) << 8);
  }
  if (const auto offset = Patterns::ptnSetDIRTS.find(reader)) {
    return static_cast<u16>(reader.u8At(*offset + 1) << 8);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<InstrumentProbe> findInstrumentProbe(ByteReader reader, const Profile& selected) {
  if (selected.id == ProfileId::Unknown) {
    return InstrumentProbe{};
  }

  InstrumentProbe probe;
  if (const auto standardOffset = Patterns::ptnLoadInstrTableAddress.find(reader)) {
    probe.tableAddress = reader.u8At(*standardOffset + 7) | (reader.u8At(*standardOffset + 10) << 8);
    if (reader.has(probe.tableAddress, 4)) {
      const u32 firstWord = reader.le32(probe.tableAddress);
      if (firstWord == 0 || firstWord == 0xffffffff) {
        probe.tableAddress += 4;
      }
    }
  } else if (const auto earlierOffset = Patterns::ptnLoadInstrTableAddressSMW.find(reader)) {
    probe.tableAddress = reader.u8At(*earlierOffset + 3) | (reader.u8At(*earlierOffset + 6) << 8);
  } else if (selected.instrumentTable == InstrumentTableAddressModel::Human) {
    if (const auto clockTowerOffset = Patterns::ptnLoadInstrTableAddressCTOW.find(reader)) {
      probe.tableAddress = reader.u8At(*clockTowerOffset + 7) | (reader.u8At(*clockTowerOffset + 10) << 8);
    } else if (const auto sosOffset = Patterns::ptnLoadInstrTableAddressSOS.find(reader)) {
      probe.tableAddress = reader.u8At(*sosOffset + 1) | (reader.u8At(*sosOffset + 4) << 8);
    } else {
      return std::nullopt;
    }
  } else if (selected.instrumentTable == InstrumentTableAddressModel::Tose) {
    const auto toseOffset = Patterns::ptnLoadInstrTableAddressYSFR.find(reader);
    if (!toseOffset) {
      return std::nullopt;
    }
    probe.dirAddress = static_cast<u16>(reader.u8At(*toseOffset + 3) << 8);
    probe.tableAddress = reader.u8At(*toseOffset + 10) | (reader.u8At(*toseOffset + 13) << 8);
  } else {
    return std::nullopt;
  }

  if (probe.dirAddress == 0) {
    const auto dir = findDirAddress(reader);
    if (!dir) {
      return std::nullopt;
    }
    probe.dirAddress = *dir;
  }
  return probe;
}

[[nodiscard]] std::optional<KonamiPercussionLayout> findKonamiPercussion(ByteReader reader) {
  const auto dispatch = Patterns::ptnKonamiPercussionDispatch.find(reader);
  const auto instrumentCommand = Patterns::ptnInstrVCmdGD3.find(reader);
  if (!dispatch || !instrumentCommand || !reader.has(*instrumentCommand + 37, 9)) {
    return std::nullopt;
  }

  // The negative instrument branch clears normal tuning, converts CA+slot to
  // an instrument index, and then enters the shared six-byte row loader.
  constexpr std::array<std::pair<u8, u8>, 8> kLoaderTail{{
      {37, 0x80},
      {38, 0xa8},
      {39, 0xca},
      {40, 0x60},
      {41, 0x88},
      {43, 0x8d},
      {44, 0x06},
      {45, 0xcf},
  }};
  if (!std::ranges::all_of(kLoaderTail, [&](const auto& expected) {
        return reader.u8At(*instrumentCommand + expected.first) == expected.second;
      })) {
    return std::nullopt;
  }

  const u16 tableAddress = reader.le16(*dispatch + 26);
  if (tableAddress >= *dispatch) {
    return std::nullopt;
  }
  const u32 tableSize = *dispatch - tableAddress;
  constexpr u8 kMaximumSlots = 0xdf - 0xca + 1;
  if (tableSize == 0 || tableSize % 3 != 0 || tableSize / 3 > kMaximumSlots) {
    return std::nullopt;
  }

  const u8 slotCount = static_cast<u8>(tableSize / 3);
  const u8 programBase = reader.u8At(*instrumentCommand + 42);
  if (programBase >= 0x80 || programBase + slotCount > 0x80) {
    return std::nullopt;
  }
  return KonamiPercussionLayout{
      .tableAddress = tableAddress,
      .slotCount = slotCount,
      .programBase = programBase,
  };
}

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto driverTraits = koei::detect(reader.slice(0, reader.size()));

  Signature signature = Signature::None;
  u8 sectionPointer = 0;
  std::optional<u16> konamiBase;
  std::optional<u16> falcomBaseAddress;

  if (driverTraits) {
    signature = Signature::Standard;
    sectionPointer = driverTraits->sectionPointerAddress;
  } else if (const auto standardOffset = Patterns::ptnIncSectionPtr.find(reader)) {
    signature = Signature::Standard;
    sectionPointer = reader.u8At(*standardOffset + 3);
  } else if (const auto konamiOffset = Patterns::ptnIncSectionPtrGD3.find(reader)) {
    signature = Signature::Konami;
    sectionPointer = reader.u8At(*konamiOffset + 3);
    const u8 basePointer = reader.u8At(*konamiOffset + 16);
    if (!reader.has(basePointer, 2)) {
      return std::nullopt;
    }
    konamiBase = reader.le16(basePointer);
  } else if (const auto toseOffset = Patterns::ptnIncSectionPtrYSFR.find(reader)) {
    signature = Signature::Tose;
    sectionPointer = reader.u8At(*toseOffset + 3);
  } else if (const auto falcomOffset = Patterns::ptnIncSectionPtrYs4.find(reader)) {
    signature = Signature::FalcomYs4;
    sectionPointer = reader.u8At(*falcomOffset + 3);
  } else {
    return std::nullopt;
  }

  const auto songList = findSongList(reader, sectionPointer, konamiBase);
  if (!songList) {
    return std::nullopt;
  }
  signature = songList->signature != Signature::None ? songList->signature : signature;
  ProfileId profileId = songList->profile;
  if (songList->konamiBaseAddress) {
    konamiBase = songList->konamiBaseAddress;
  }
  falcomBaseAddress = songList->falcomBaseAddress;

  const auto commands = findVoiceCommands(reader);
  if (!commands) {
    return std::nullopt;
  }
  signature = commands->signature != Signature::None ? commands->signature : signature;
  profileId = commands->profile != ProfileId::Unknown ? commands->profile : profileId;

  std::vector<u8> durationRateTable;
  std::vector<u8> volumeTable;
  const auto loadNoteTables = [&](const Pattern& pattern, u8 durationOffset, u8 volumeOffset) {
    const auto offset = pattern.find(reader);
    if (!offset) {
      return false;
    }
    durationRateTable = readTable(reader, reader.le16(*offset + durationOffset), 8);
    volumeTable = readTable(reader, reader.le16(*offset + volumeOffset), 16);
    return !durationRateTable.empty() && !volumeTable.empty();
  };
  static_cast<void>(
      loadNoteTables(Patterns::ptnDispatchNoteYI, 6, 16) || loadNoteTables(Patterns::ptnDispatchNoteGD3, 6, 16) ||
      loadNoteTables(Patterns::ptnDispatchNoteYSFR, 16, 4) || loadNoteTables(Patterns::ptnDispatchNoteYs4, 6, 15));

  u32 instrumentCommandOffset = 0;
  if (profileId == ProfileId::Standard) {
    profileId = classifyStandard(reader, *commands, konamiBase, instrumentCommandOffset);
  }
  const Profile& selected = profile(profileId);
  if (selected.intelli != IntelliMode::None) {
    signature = Signature::Intelligent;
  }

  u8 quintetBase = 0;
  u16 quintetLookup = 0;
  if (selected.programs == ProgramResolver::QuintetActRBase) {
    signature = Signature::Quintet;
    const u16 address = reader.le16(instrumentCommandOffset + 18);
    if (!reader.has(address, 1)) {
      return std::nullopt;
    }
    quintetBase = reader.u8At(address);
  } else if (selected.programs == ProgramResolver::QuintetLookup) {
    signature = Signature::Quintet;
    const u32 operand = instrumentCommandOffset + (selected.id == ProfileId::QuintetTs ? 18 : 19);
    quintetLookup = reader.le16(operand);
  }

  const auto resolveAddress = [&](u16 raw, u16 falcomOffset) {
    switch (selected.addresses) {
      case AddressModel::KonamiBase:
        return static_cast<u16>(konamiBase.value_or(0) + raw);
      case AddressModel::FalcomBaseOffset:
        return static_cast<u16>(falcomOffset + raw);
      case AddressModel::Direct:
      default:
        return raw;
    }
  };
  const auto sectionListPointer = [&](u32 songPointer) {
    u16 address = reader.le16(songPointer);
    if (selected.addresses == AddressModel::KonamiBase) {
      address = resolveAddress(address, 0);
    }
    return address;
  };
  std::optional<KonamiPercussionLayout> konamiPercussion;
  std::optional<u8> fixedPercussionBase =
      detectFixedPercussionBase(reader, selected.base == BaseProfile::Earlier ? u8{0xd0} : u8{0xca});
  u8 tempoTimerTarget = kStandardTimerTarget;
  if (selected.id == ProfileId::Konami) {
    konamiPercussion = findKonamiPercussion(reader);
    if (konamiPercussion) {
      fixedPercussionBase = konamiPercussion->programBase;
    }
    if (const auto target = detectKonamiTempoTimerTarget(reader)) {
      tempoTimerTarget = *target;
    }
  }
  Layout baseLayout{
      .signature = signature,
      .profile = profileId,
      .songListAddress = songList->address,
      .sectionPointerAddress = sectionPointer,
      .sectionTrackCount = driverTraits ? driverTraits->bgmTrackCount : static_cast<u8>(kTrackCount),
      .konamiBaseAddress = konamiBase.value_or(0),
      .quintetBgmInstrumentBase = quintetBase,
      .quintetInstrumentLookupAddress = quintetLookup,
      .fixedPercussionBase = fixedPercussionBase,
      .konamiPercussion = konamiPercussion,
      .tempoTimerTarget = tempoTimerTarget,
      .volumeTable = std::move(volumeTable),
      .durationRateTable = std::move(durationRateTable),
  };

  if (selected.base == BaseProfile::Earlier) {
    if (const auto offset = Patterns::ptnEarlierPercussionTable.find(reader)) {
      const u16 address = reader.u8At(*offset + 6) | (reader.u8At(*offset + 9) << 8);
      if (reader.has(address, 6)) {
        baseLayout.percussionTableAddress = address;
      }
    }
  }

  if (const auto instruments = findInstrumentProbe(reader, selected)) {
    if (instruments->tableAddress != 0) {
      baseLayout.instrumentTableAddress = instruments->tableAddress;
    }
    if (instruments->dirAddress != 0) {
      baseLayout.spcDirAddress = instruments->dirAddress;
    }
  }

  if (selected.instruments == InstrumentLayout::KonamiTuningTable) {
    if (const auto offset = Patterns::ptnInstrVCmdGD3.find(reader)) {
      const u16 low = reader.le16(*offset + 10);
      const u16 high = reader.le16(*offset + 14);
      if (high > low && high - low <= 0x7f) {
        baseLayout.konamiTuningTableAddress = low;
        baseLayout.konamiTuningTableSize = static_cast<u8>(high - low);
      }
    }
  }

  struct SongEntry {
    u8 index = 0;
    u16 playlistAddress = 0;
    u16 falcomOffset = 0;
  };
  // Inspect the full addressable table: games such as Mario Paint leave
  // unloaded holes, while Hyper Zone uses song slots beyond 31.
  constexpr u8 kSongSlotCount = 0x80;
  std::vector<SongEntry> entries;
  entries.reserve(kSongSlotCount);
  u32 songListEnd = kAramSize;
  for (u8 song = 0; song < kSongSlotCount; ++song) {
    const u32 pointer = songList->address + song * 2;
    if (!reader.has(pointer, 2) || pointer >= songListEnd) {
      break;
    }
    const u16 playlistAddress = sectionListPointer(pointer);
    if (playlistAddress < 0x100 || playlistAddress >= 0xfff0 || !reader.has(playlistAddress, 2)) {
      continue;
    }
    if (playlistAddress >= pointer) {
      songListEnd = std::min(songListEnd, static_cast<u32>(playlistAddress));
    }
    entries.push_back(SongEntry{
        .index = song,
        .playlistAddress = playlistAddress,
        .falcomOffset = selected.addresses == AddressModel::FalcomBaseOffset && falcomBaseAddress
                            ? static_cast<u16>(playlistAddress - *falcomBaseAddress)
                            : u16{0},
    });
  }

  const SongEntry* currentSong = nullptr;
  if (reader.has(sectionPointer, 2)) {
    const u16 currentAddress = reader.le16(sectionPointer);
    if (currentAddress >= 0x100 && currentAddress < 0xfff0) {
      for (const SongEntry& entry : entries) {
        u16 address = entry.playlistAddress;
        for (u8 command = 0; reader.has(address, 2) && command < 64; ++command) {
          if (address == currentAddress) {
            currentSong = &entry;
            break;
          }
          const u16 value = reader.le16(address);
          if (value == 0 || isInfinitePlaylistRepeat(selected.playlist, value)) {
            break;
          }
          address = static_cast<u16>(address + (value <= 0xff ? 4 : 2));
        }
        if (currentSong) {
          break;
        }
      }
    }
  }

  std::vector<const SongEntry*> candidates;
  const auto queueCandidate = [&](const SongEntry* entry) {
    if (entry != nullptr && std::ranges::none_of(candidates, [entry](const SongEntry* queued) {
          return queued->playlistAddress == entry->playlistAddress;
        })) {
      candidates.push_back(entry);
    }
  };

  if (driverTraits) {
    const auto requested = std::ranges::find(entries, driverTraits->requestedSong, &SongEntry::index);
    if (requested != entries.end()) {
      queueCandidate(&*requested);
    }
  }

  // Some rips are captured before the driver consumes the song request. Detect
  // the command mirror it actually reads instead of assuming a driver profile.
  if (const auto requestRead = Patterns::ptnReadSongRequestPort.find(reader)) {
    const u8 mirror = reader.u8At(*requestRead + 1);
    if (mirror < 4) {
      const u8 request = reader.u8At(0xf4 + mirror);
      const u8 requestedSong = request & 0x1f;
      if (request != 0xff && requestedSong != 0) {
        const auto entry = std::ranges::find(entries, requestedSong, &SongEntry::index);
        if (entry != entries.end()) {
          queueCandidate(&*entry);
        }
      }
    }
  }
  queueCandidate(currentSong);

  if (driverTraits && candidates.empty()) {
    return std::nullopt;
  }
  for (const SongEntry& entry : entries) {
    queueCandidate(&entry);
  }

  for (const SongEntry* entry : candidates) {
    Layout layout = baseLayout;
    layout.songIndex = entry->index;
    layout.playlistAddress = entry->playlistAddress;
    layout.falcomBaseOffset = entry->falcomOffset;
    if (!isValidPlaylist(reader, layout)) {
      continue;
    }
    return layout;
  }
  return std::nullopt;
}

}  // namespace vgmtrans::formats::nin_snes
