/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NinSnesInstr.h"
#include "NinSnesSeq.h"
#include "ScannerManager.h"

#include <array>
#include <optional>

namespace vgmtrans::scanners {
ScannerRegistration<NinSnesScanner> s_nin_snes("NinSnes", {"spc"});
}

namespace {

struct NinSnesSongListInfo {
  uint32_t address = 0;
  NinSnesSignatureId signature = NinSnesSignatureId::None;
  NinSnesProfileId profileId = NinSnesProfileId::Unknown;
  uint16_t konamiBaseAddress = 0xffff;
  uint16_t falcomBaseAddress = 0xffff;
};

struct NinSnesVoiceCommandInfo {
  uint8_t firstVoiceCmd = 0;
  uint16_t addressTable = 0;
  uint16_t lengthTable = 0;
  uint8_t numCommands = 0;
  NinSnesSignatureId signature = NinSnesSignatureId::None;
  NinSnesProfileId profileId = NinSnesProfileId::Unknown;
};

struct NinSnesInstrumentProbeInfo {
  uint32_t tableAddress = 0;
  uint16_t dirAddress = 0;
};

template <size_t N>
bool matchNinSnesByteTable(RawFile* file, uint16_t offset, const std::array<uint8_t, N>& table) {
  return file->matchBytes(table.data(), offset, table.size());
}

std::vector<uint8_t> readNinSnesByteTable(RawFile* file, uint16_t address, uint8_t length) {
  std::vector<uint8_t> table;
  table.reserve(length);
  for (uint8_t offset = 0; offset < length; offset++) {
    table.push_back(file->readByte(address + offset));
  }
  return table;
}

uint8_t countNinSnesVoiceCommands(uint8_t firstVoiceCmd, uint16_t addressTable,
                                  uint16_t lengthTable) {
  if (addressTable < lengthTable && addressTable + (0x100 - firstVoiceCmd) * 2 >= lengthTable) {
    return static_cast<uint8_t>((lengthTable - addressTable) / 2);
  }
  return 0;
}

constexpr std::array<uint8_t, 27> kStandardVcmdLengthTable = {
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01,
    0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01,
};

constexpr std::array<uint8_t, 40> kIntelliFe3VcmdLengthTable = {
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01,
    0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x02, 0x02,
};

constexpr std::array<uint8_t, 36> kIntelliFe4VcmdLengthTable = {
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03,
    0x00, 0x01, 0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03,
    0x03, 0x03, 0x01, 0x00, 0x00, 0x02, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01,
};

constexpr std::array<uint8_t, 36> kIntelliTaVcmdLengthTable = {
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03,
    0x00, 0x01, 0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03,
    0x03, 0x03, 0x01, 0x00, 0x00, 0x02, 0x02, 0x00, 0x01, 0x01, 0x01, 0x01,
};

}  // namespace

void NinSnesScanner::scan(RawFile* file, void* info) {
  size_t nFileLength = file->size();
  if (nFileLength == 0x10000) {
    searchForNinSnesFromARAM(file);
  } else {
    // Search from ROM unimplemented
  }
}

void NinSnesScanner::loadFromScanResult(RawFile* file, const NinSnesScanResult& scanResult) {
  auto* newSeq = new NinSnesSeq(file, scanResult);
  if (!newSeq->loadVGMFile()) {
    delete newSeq;
    return;
  }

  if (scanResult.profile == NinSnesProfileId::Unknown || scanResult.instrTableAddr == 0 ||
      scanResult.spcDirAddr == 0) {
    return;
  }

  auto* newInstrSet = new NinSnesInstrSet(file, scanResult);
  if (!newInstrSet->loadVGMFile()) {
    delete newInstrSet;
    return;
  }
}

void NinSnesScanner::searchForNinSnesFromARAM(RawFile* file) {
  NinSnesProfileId profileId = NinSnesProfileId::Unknown;
  NinSnesSignatureId signature = NinSnesSignatureId::None;

  std::string basefilename = file->stem();
  std::string name = file->tag.hasTitle() ? file->tag.title : basefilename;

  // get section pointer address
  uint32_t ofsIncSectionPtr;
  uint8_t addrSectionPtr;
  uint16_t konamiBaseAddress = 0xffff;
  uint16_t falcomBaseAddress = 0xffff;
  uint16_t falcomBaseOffset = 0;
  if (file->searchBytePattern(ptnIncSectionPtr, ofsIncSectionPtr)) {
    signature = NinSnesSignatureId::Standard;
    addrSectionPtr = file->readByte(ofsIncSectionPtr + 3);
  }
  // DERIVED VERSIONS
  else if (file->searchBytePattern(ptnIncSectionPtrGD3, ofsIncSectionPtr)) {
    signature = NinSnesSignatureId::Konami;
    uint8_t konamiBaseAddressPtr = file->readByte(ofsIncSectionPtr + 16);
    addrSectionPtr = file->readByte(ofsIncSectionPtr + 3);
    konamiBaseAddress = file->readShort(konamiBaseAddressPtr);
  } else if (file->searchBytePattern(ptnIncSectionPtrYSFR, ofsIncSectionPtr)) {
    signature = NinSnesSignatureId::Tose;
    addrSectionPtr = file->readByte(ofsIncSectionPtr + 3);
  } else if (file->searchBytePattern(ptnIncSectionPtrYs4, ofsIncSectionPtr)) {
    signature = NinSnesSignatureId::FalcomYs4;
    addrSectionPtr = file->readByte(ofsIncSectionPtr + 3);
  } else {
    return;
  }

  auto findSongList = [&]() -> std::optional<NinSnesSongListInfo> {
    const BytePattern ptnInitSectionPtr = makeInitSectionPtrPattern(addrSectionPtr);
    const BytePattern ptnInitSectionPtrYI = makeInitSectionPtrYIPattern(addrSectionPtr);
    const BytePattern ptnInitSectionPtrSMW = makeInitSectionPtrSMWPattern(addrSectionPtr);
    const BytePattern ptnInitSectionPtrGD3 = makeInitSectionPtrGD3Pattern(addrSectionPtr);
    const BytePattern ptnInitSectionPtrYSFR = makeInitSectionPtrYSFRPattern(addrSectionPtr);
    const BytePattern ptnInitSectionPtrTS = makeInitSectionPtrTSPattern(addrSectionPtr);
    const BytePattern ptnInitSectionPtrYs4 = makeInitSectionPtrYs4Pattern(addrSectionPtr);

    uint32_t ofsInitSectionPtr;
    NinSnesSongListInfo info;

    if (file->searchBytePattern(ptnInitSectionPtr, ofsInitSectionPtr)) {
      if (file->searchBytePattern(ptnInitSectionPtrYs4, ofsInitSectionPtr)) {
        info.signature = NinSnesSignatureId::FalcomYs4;
        info.profileId = NinSnesProfileId::FalcomYs4;
        info.address = file->readShort(ofsInitSectionPtr + 5);
        info.falcomBaseAddress = file->readByte(ofsInitSectionPtr + 13) << 8;
      } else {
        info.address = file->readShort(ofsInitSectionPtr + 5);
      }
      return info;
    }

    if (file->searchBytePattern(ptnInitSectionPtrYI, ofsInitSectionPtr)) {
      info.address = file->readShort(ofsInitSectionPtr + 12);
      return info;
    }

    if (file->searchBytePattern(ptnInitSectionPtrSMW, ofsInitSectionPtr)) {
      info.signature = NinSnesSignatureId::Earlier;
      info.address = file->readShort(ofsInitSectionPtr + 3);
      return info;
    }

    if (file->searchBytePattern(ptnInitSectionPtrGD3, ofsInitSectionPtr)) {
      info.address = file->readShort(ofsInitSectionPtr + 8);
      if (konamiBaseAddress == 0xffff) {
        // Parodius Da! does not have base address
        info.konamiBaseAddress = 0;
      }
      return info;
    }

    if (file->searchBytePattern(ptnInitSectionPtrTS, ofsInitSectionPtr)) {
      info.signature = NinSnesSignatureId::Quintet;
      const uint16_t addrSongListPtr = file->readShort(ofsInitSectionPtr + 1);
      info.address = file->readShort(addrSongListPtr);
      return info;
    }

    if (file->searchBytePattern(ptnInitSectionPtrHE4, ofsInitSectionPtr)) {
      info.address = file->readByte(ofsInitSectionPtr + 4) << 8;
      return info;
    }

    if (file->searchBytePattern(ptnInitSectionPtrYSFR, ofsInitSectionPtr)) {
      const uint8_t addrSongListPtr = file->readByte(ofsInitSectionPtr + 2);
      const BytePattern ptnInitSongListPtrYSFR = makeInitSongListPtrYSFRPattern(addrSongListPtr);

      uint32_t ofsInitSongListPtr;
      if (!file->searchBytePattern(ptnInitSongListPtrYSFR, ofsInitSongListPtr)) {
        return std::nullopt;
      }

      info.address =
          file->readByte(ofsInitSongListPtr + 1) | (file->readByte(ofsInitSongListPtr + 4) << 8);
      return info;
    }

    return std::nullopt;
  };

  const auto songListInfo = findSongList();
  if (!songListInfo) {
    return;
  }

  const uint32_t addrSongList = songListInfo->address;
  if (songListInfo->signature != NinSnesSignatureId::None) {
    signature = songListInfo->signature;
  }
  if (songListInfo->profileId != NinSnesProfileId::Unknown) {
    profileId = songListInfo->profileId;
  }
  if (songListInfo->konamiBaseAddress != 0xffff) {
    konamiBaseAddress = songListInfo->konamiBaseAddress;
  }
  if (songListInfo->falcomBaseAddress != 0xffff) {
    falcomBaseAddress = songListInfo->falcomBaseAddress;
  }

  // ACQUIRE VOICE COMMAND LIST & DETECT ENGINE VERSION
  // (Minor classification for derived versions should come later as far as possible)
  auto findVoiceCommands = [&]() -> std::optional<NinSnesVoiceCommandInfo> {
    uint32_t ofsBranchForVcmd;
    NinSnesVoiceCommandInfo info;

    if (file->searchBytePattern(ptnJumpToVcmdYSFR, ofsBranchForVcmd)) {
      info.addressTable = file->readShort(ofsBranchForVcmd + 9);

      uint32_t ofsReadVcmdLength;
      if (!file->searchBytePattern(ptnReadVcmdLengthYSFR, ofsReadVcmdLength)) {
        return std::nullopt;
      }

      info.firstVoiceCmd = file->readByte(ofsReadVcmdLength + 2);
      info.lengthTable = file->readShort(ofsReadVcmdLength + 7);
      info.profileId =
          info.firstVoiceCmd == 0xe0 ? NinSnesProfileId::Tose : NinSnesProfileId::Unknown;
      info.numCommands =
          countNinSnesVoiceCommands(info.firstVoiceCmd, info.addressTable, info.lengthTable);
      return info;
    }

    if (file->searchBytePattern(ptnJumpToVcmdYs4, ofsBranchForVcmd)) {
      info.addressTable = file->readShort(ofsBranchForVcmd + 10);

      uint32_t ofsReadVcmdLength;
      if (!file->searchBytePattern(ptnReadVcmdLengthYs4, ofsReadVcmdLength)) {
        return std::nullopt;
      }

      info.firstVoiceCmd = file->readByte(ofsReadVcmdLength + 1);
      info.lengthTable = file->readShort(ofsReadVcmdLength + 9) + info.firstVoiceCmd;
      info.numCommands =
          countNinSnesVoiceCommands(info.firstVoiceCmd, info.addressTable, info.lengthTable);
      return info;
    }

    if (file->searchBytePattern(ptnBranchForVcmdReadahead, ofsBranchForVcmd)) {
      info.firstVoiceCmd = file->readByte(ofsBranchForVcmd + 5);
    } else if (file->searchBytePattern(ptnBranchForVcmd, ofsBranchForVcmd)) {
      // this search often finds a wrong code, but some games still need it (for example, Human
      // games)
      info.firstVoiceCmd = file->readByte(ofsBranchForVcmd + 1);
    } else {
      return std::nullopt;
    }

    uint32_t ofsJumpToVcmd;
    if (file->searchBytePattern(ptnJumpToVcmd, ofsJumpToVcmd)) {
      if (file->searchBytePattern(ptnJumpToVcmdCTOW, ofsJumpToVcmd)) {
        info.signature = NinSnesSignatureId::Human;
        info.profileId = NinSnesProfileId::Human;
        info.addressTable = file->readShort(ofsJumpToVcmd + 10);
        info.lengthTable = file->readShort(ofsJumpToVcmd + 17);
      } else {
        info.profileId = NinSnesProfileId::Standard;
        info.addressTable = file->readShort(ofsJumpToVcmd + 7) + ((info.firstVoiceCmd * 2) & 0xff);
        info.lengthTable = file->readShort(ofsJumpToVcmd + 14) + (info.firstVoiceCmd & 0x7f);
      }
    } else if (file->searchBytePattern(ptnJumpToVcmdSMW, ofsJumpToVcmd)) {
      uint32_t ofsReadVcmdLength;
      if (!file->searchBytePattern(ptnReadVcmdLengthSMW, ofsReadVcmdLength)) {
        return std::nullopt;
      }

      info.profileId = NinSnesProfileId::Earlier;
      info.addressTable = file->readShort(ofsJumpToVcmd + 5) + ((info.firstVoiceCmd * 2) & 0xff);
      info.lengthTable = file->readShort(ofsReadVcmdLength + 9) + info.firstVoiceCmd;
    } else {
      return std::nullopt;
    }

    info.numCommands =
        countNinSnesVoiceCommands(info.firstVoiceCmd, info.addressTable, info.lengthTable);
    return info;
  };

  const auto voiceCommandInfo = findVoiceCommands();
  if (!voiceCommandInfo) {
    return;
  }

  const uint8_t firstVoiceCmd = voiceCommandInfo->firstVoiceCmd;
  const uint16_t addrVoiceCmdAddressTable = voiceCommandInfo->addressTable;
  const uint16_t addrVoiceCmdLengthTable = voiceCommandInfo->lengthTable;
  const uint8_t numOfVoiceCmd = voiceCommandInfo->numCommands;
  if (voiceCommandInfo->signature != NinSnesSignatureId::None) {
    signature = voiceCommandInfo->signature;
  }
  if (voiceCommandInfo->profileId != NinSnesProfileId::Unknown) {
    profileId = voiceCommandInfo->profileId;
  }

  // TRY TO GRAB NOTE VOLUME/DURATION TABLE
  uint32_t ofsDispatchNote = 0;
  uint16_t addrDurRateTable = 0;
  uint16_t addrVolumeTable = 0;
  std::vector<uint8_t> durRateTable;
  std::vector<uint8_t> volumeTable;
  auto loadNoteTable = [&](const BytePattern& pattern, uint8_t durTableOffset,
                           uint8_t volumeTableOffset) -> bool {
    if (!file->searchBytePattern(pattern, ofsDispatchNote)) {
      return false;
    }

    addrDurRateTable = file->readShort(ofsDispatchNote + durTableOffset);
    durRateTable = readNinSnesByteTable(file, addrDurRateTable, 8);
    addrVolumeTable = file->readShort(ofsDispatchNote + volumeTableOffset);
    volumeTable = readNinSnesByteTable(file, addrVolumeTable, 16);
    return true;
  };

  loadNoteTable(ptnDispatchNoteYI, 6, 16) || loadNoteTable(ptnDispatchNoteGD3, 6, 16) ||
      loadNoteTable(ptnDispatchNoteYSFR, 16, 4) || loadNoteTable(ptnDispatchNoteYs4, 6, 15);

  // CLASSIFY DERIVED VERSIONS (fix false-positive)
  uint32_t ofsInstrVCmd = 0;
  auto classifyIntelligentProfile = [&]() -> NinSnesProfileId {
    uint32_t ofsIntelliVCmdFA;
    if (!file->searchBytePattern(ptnIntelliVCmdFA, ofsIntelliVCmdFA)) {
      return NinSnesProfileId::Unknown;
    }

    if (file->searchBytePattern(ptnDispatchNoteFE3, ofsDispatchNote)) {
      return firstVoiceCmd == 0xd6 && matchNinSnesByteTable(file, addrVoiceCmdLengthTable,
                                                            kIntelliFe3VcmdLengthTable)
                 ? NinSnesProfileId::IntelliFe3
                 : NinSnesProfileId::Unknown;
    }

    if (file->searchBytePattern(ptnDispatchNoteFE4, ofsDispatchNote)) {
      return firstVoiceCmd == 0xda && matchNinSnesByteTable(file, addrVoiceCmdLengthTable,
                                                            kIntelliFe4VcmdLengthTable)
                 ? NinSnesProfileId::IntelliFe4
                 : NinSnesProfileId::Unknown;
    }

    return firstVoiceCmd == 0xda &&
                   matchNinSnesByteTable(file, addrVoiceCmdLengthTable, kIntelliTaVcmdLengthTable)
               ? NinSnesProfileId::IntelliTa
               : NinSnesProfileId::Unknown;
  };

  auto classifyStandardProfile = [&]() -> NinSnesProfileId {
    if (konamiBaseAddress != 0xffff) {
      return NinSnesProfileId::Konami;
    }

    if (file->searchBytePattern(ptnDispatchNoteLEM, ofsDispatchNote)) {
      return firstVoiceCmd == 0xe0 ? NinSnesProfileId::Lemmings : NinSnesProfileId::Unknown;
    }

    if (firstVoiceCmd != 0xe0 ||
        !matchNinSnesByteTable(file, addrVoiceCmdLengthTable, kStandardVcmdLengthTable)) {
      return classifyIntelligentProfile();
    }

    const bool canonicalVcmdLayout =
        addrVoiceCmdAddressTable + (kStandardVcmdLengthTable.size() * 2) == addrVoiceCmdLengthTable;
    if (canonicalVcmdLayout) {
      uint32_t ofsWriteVolume;

      if (file->searchBytePattern(ptnWriteVolumeKSS, ofsWriteVolume)) {
        return NinSnesProfileId::Hal;
      }
      if (file->searchBytePattern(ptnInstrVCmdACTR, ofsInstrVCmd)) {
        return NinSnesProfileId::QuintetActR;
      }
      if (file->searchBytePattern(ptnInstrVCmdACTR2, ofsInstrVCmd)) {
        return NinSnesProfileId::QuintetActR2;
      }
      return NinSnesProfileId::Standard;
    }

    uint32_t ofsRD1VCmd_FA_FE;
    uint32_t ofsRD2VCmdInstrADSR;
    const bool hasQuintetLookupTail =
        numOfVoiceCmd == 32 && file->readByte(addrVoiceCmdLengthTable + 31) == 1;

    if (file->searchBytePattern(ptnRD1VCmd_FA_FE, ofsRD1VCmd_FA_FE)) {
      return NinSnesProfileId::Rd1;
    }
    if (file->searchBytePattern(ptnRD2VCmdInstrADSR, ofsRD2VCmdInstrADSR)) {
      return NinSnesProfileId::Rd2;
    }
    if (hasQuintetLookupTail && file->searchBytePattern(ptnInstrVCmdACTR2, ofsInstrVCmd)) {
      return NinSnesProfileId::QuintetIog;
    }
    if (hasQuintetLookupTail && file->searchBytePattern(ptnInstrVCmdTS, ofsInstrVCmd)) {
      return NinSnesProfileId::QuintetTs;
    }
    return NinSnesProfileId::Standard;
  };

  if (profileId == NinSnesProfileId::Standard) {
    profileId = classifyStandardProfile();
  }

  const auto& profile = getNinSnesProfile(profileId);
  if (profile.intelliMode != NinSnesIntelliModeId::None) {
    signature = NinSnesSignatureId::Intelligent;
  }

  // Quintet: ACQUIRE INSTRUMENT BASE:
  uint8_t quintetBGMInstrBase = 0;
  uint16_t quintetAddrBGMInstrLookup = 0;
  switch (profile.programResolver) {
    case NinSnesProgramResolverId::QuintetActRBase: {
      signature = NinSnesSignatureId::Quintet;
      uint16_t addrBGMInstrBase = file->readShort(ofsInstrVCmd + 18);
      quintetBGMInstrBase = file->readByte(addrBGMInstrBase);
      break;
    }

    case NinSnesProgramResolverId::QuintetLookup:
      signature = NinSnesSignatureId::Quintet;
      quintetAddrBGMInstrLookup =
          file->readShort(ofsInstrVCmd + (profile.id == NinSnesProfileId::QuintetTs ? 18 : 19));
      break;

    case NinSnesProgramResolverId::Direct:
    case NinSnesProgramResolverId::StandardPercussion:
    case NinSnesProgramResolverId::IntelliTaOverride:
    default:
      break;
  }

  auto readSectionListPtr = [&](uint32_t addrSectionListPtr) -> uint16_t {
    uint16_t firstSectionPtr = file->readShort(addrSectionListPtr);
    if (profile.addressModel == NinSnesAddressModelId::KonamiBase) {
      firstSectionPtr =
          convertNinSnesAddress(profile, firstSectionPtr, konamiBaseAddress, falcomBaseOffset);
    }
    return firstSectionPtr;
  };

  auto updateFalcomBaseOffset = [&](uint16_t firstSectionPtr) {
    if (profile.addressModel == NinSnesAddressModelId::FalcomBaseOffset) {
      falcomBaseOffset = firstSectionPtr - falcomBaseAddress;
    }
  };

  auto hasIllegalTrackPointers = [&](uint16_t addrFirstSection) -> bool {
    for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
      uint16_t addrTrackStart = file->readShort(addrFirstSection + trackIndex * 2);
      if (addrTrackStart == 0) {
        continue;
      }
      if (addrTrackStart == 0xffff) {
        return true;
      }

      addrTrackStart =
          convertNinSnesAddress(profile, addrTrackStart, konamiBaseAddress, falcomBaseOffset);
      if ((addrTrackStart & 0xff00) == 0 || addrTrackStart == 0xffff) {
        return true;
      }
    }
    return false;
  };

  auto findSongListLength = [&]() -> uint8_t {
    uint8_t songListLength = 1;
    uint16_t addrSectionListCutoff = 0xffff;

    for (uint8_t songIndex = 1; songIndex <= 0x7f; songIndex++) {
      const uint32_t addrSectionListPtr = addrSongList + songIndex * 2;
      if (addrSectionListPtr >= addrSectionListCutoff) {
        break;
      }

      const uint16_t firstSectionPtr = readSectionListPtr(addrSectionListPtr);
      if (firstSectionPtr == 0) {
        continue;
      }
      if ((firstSectionPtr & 0xff00) == 0 || firstSectionPtr == 0xffff) {
        break;
      }
      if (firstSectionPtr >= addrSectionListPtr) {
        addrSectionListCutoff = std::min(addrSectionListCutoff, firstSectionPtr);
      }

      uint16_t addrFirstSection = file->readShort(firstSectionPtr);
      if (addrFirstSection < 0x0100) {
        continue;
      }

      updateFalcomBaseOffset(firstSectionPtr);
      addrFirstSection =
          convertNinSnesAddress(profile, addrFirstSection, konamiBaseAddress, falcomBaseOffset);
      if (addrFirstSection + 16 > 0x10000 || hasIllegalTrackPointers(addrFirstSection)) {
        break;
      }

      songListLength = songIndex + 1;
    }

    return songListLength;
  };

  auto findCurrentSongIndex = [&](uint8_t songListLength) -> std::optional<uint8_t> {
    const uint16_t addrCurrentSection = file->readShort(addrSectionPtr);
    if (addrCurrentSection < 0x0100 || addrCurrentSection >= 0xfff0) {
      return std::nullopt;
    }

    for (uint8_t songIndex = 0; songIndex <= songListLength; songIndex++) {
      const uint32_t addrSectionListPtr = addrSongList + songIndex * 2;
      if (addrSectionListPtr == 0 || addrSectionListPtr == 0xffff) {
        continue;
      }

      const uint16_t firstSectionPtr = readSectionListPtr(addrSectionListPtr);
      updateFalcomBaseOffset(firstSectionPtr);
      if (firstSectionPtr > addrCurrentSection ||
          (addrCurrentSection % 2) != (firstSectionPtr % 2)) {
        continue;
      }

      uint16_t curAddress = firstSectionPtr;
      uint8_t sectionCount = 0;
      while (curAddress >= 0x0100 && curAddress < 0xfff0 && sectionCount < 32) {
        const uint16_t addrSection =
            readNinSnesAddress(profile, file, curAddress, konamiBaseAddress, falcomBaseOffset);
        if (curAddress == addrCurrentSection) {
          return songIndex;
        }
        if ((addrSection & 0xff00) == 0) {
          break;
        }

        curAddress += 2;
        sectionCount++;
      }
    }

    return std::nullopt;
  };

  const uint8_t songListLength = findSongListLength();
  const auto guessedSongIndex = findCurrentSongIndex(songListLength);
  if (!guessedSongIndex) {
    return;
  }

  // load the song
  uint16_t addrSongStart = file->readShort(addrSongList + (*guessedSongIndex) * 2);
  if (profile.addressModel == NinSnesAddressModelId::KonamiBase) {
    addrSongStart =
        convertNinSnesAddress(profile, addrSongStart, konamiBaseAddress, falcomBaseOffset);
  }

  auto findDirAddress = [&]() -> std::optional<uint16_t> {
    uint32_t ofsSetDIR;
    if (file->searchBytePattern(ptnSetDIR, ofsSetDIR)) {
      return static_cast<uint16_t>(file->readByte(ofsSetDIR + 4) << 8);
    }
    if (file->searchBytePattern(ptnSetDIRYI, ofsSetDIR)) {
      return static_cast<uint16_t>(file->readByte(ofsSetDIR + 1) << 8);
    }
    if (file->searchBytePattern(ptnSetDIRVS, ofsSetDIR)) {
      const u16 spcDirAddrPtr = file->readShort(ofsSetDIR + 1);
      return static_cast<uint16_t>(file->readByte(spcDirAddrPtr) << 8);
    }
    if (file->searchBytePattern(ptnSetDIRSMW, ofsSetDIR)) {
      return static_cast<uint16_t>(file->readByte(ofsSetDIR + 9) << 8);
    }
    if (file->searchBytePattern(ptnSetDIRCTOW, ofsSetDIR)) {
      return static_cast<uint16_t>(file->readByte(ofsSetDIR + 3) << 8);
    }
    if (file->searchBytePattern(ptnSetDIRTS, ofsSetDIR)) {
      return static_cast<uint16_t>(file->readByte(ofsSetDIR + 1) << 8);
    }
    return std::nullopt;
  };

  auto findInstrumentProbeInfo = [&]() -> std::optional<NinSnesInstrumentProbeInfo> {
    if (profile.id == NinSnesProfileId::Unknown) {
      return NinSnesInstrumentProbeInfo{};
    }

    uint32_t ofsLoadInstrTableAddressASM;
    NinSnesInstrumentProbeInfo info;
    if (file->searchBytePattern(ptnLoadInstrTableAddress, ofsLoadInstrTableAddressASM)) {
      info.tableAddress = file->readByte(ofsLoadInstrTableAddressASM + 7) |
                          (file->readByte(ofsLoadInstrTableAddressASM + 10) << 8);
      const u32 firstWord = file->readWord(info.tableAddress);
      if (firstWord == 0 || firstWord == 0xFFFFFFFF) {
        info.tableAddress += 4;
      }
    } else if (file->searchBytePattern(ptnLoadInstrTableAddressSMW, ofsLoadInstrTableAddressASM)) {
      info.tableAddress = file->readByte(ofsLoadInstrTableAddressASM + 3) |
                          (file->readByte(ofsLoadInstrTableAddressASM + 6) << 8);
    } else {
      switch (profile.instrTableAddressModel) {
        case NinSnesInstrTableAddressModelId::Human:
          if (file->searchBytePattern(ptnLoadInstrTableAddressCTOW, ofsLoadInstrTableAddressASM)) {
            info.tableAddress = file->readByte(ofsLoadInstrTableAddressASM + 7) |
                                (file->readByte(ofsLoadInstrTableAddressASM + 10) << 8);
          } else if (file->searchBytePattern(ptnLoadInstrTableAddressSOS,
                                             ofsLoadInstrTableAddressASM)) {
            info.tableAddress = file->readByte(ofsLoadInstrTableAddressASM + 1) |
                                (file->readByte(ofsLoadInstrTableAddressASM + 4) << 8);
          } else {
            return std::nullopt;
          }
          break;

        case NinSnesInstrTableAddressModelId::Tose:
          if (!file->searchBytePattern(ptnLoadInstrTableAddressYSFR, ofsLoadInstrTableAddressASM)) {
            return std::nullopt;
          }
          info.dirAddress = file->readByte(ofsLoadInstrTableAddressASM + 3) << 8;
          info.tableAddress = file->readByte(ofsLoadInstrTableAddressASM + 10) |
                              (file->readByte(ofsLoadInstrTableAddressASM + 13) << 8);
          break;

        case NinSnesInstrTableAddressModelId::Standard:
        default:
          return std::nullopt;
      }
    }

    if (info.dirAddress == 0) {
      const auto dirAddress = findDirAddress();
      if (!dirAddress) {
        return std::nullopt;
      }
      info.dirAddress = *dirAddress;
    }

    return info;
  };

  uint32_t addrInstrTable = 0;
  uint16_t spcDirAddr = 0;
  const auto instrumentProbeInfo = findInstrumentProbeInfo();
  if (!instrumentProbeInfo) {
    return;
  }
  addrInstrTable = instrumentProbeInfo->tableAddress;
  spcDirAddr = instrumentProbeInfo->dirAddress;

  uint16_t konamiTuningTableAddress = 0;
  uint8_t konamiTuningTableSize = 0;
  if (profile.instrumentLayout == NinSnesInstrumentLayoutId::KonamiTuningTable) {
    if (file->searchBytePattern(ptnInstrVCmdGD3, ofsInstrVCmd)) {
      uint16_t konamiAddrTuningTableLow = file->readShort(ofsInstrVCmd + 10);
      uint16_t konamiAddrTuningTableHigh = file->readShort(ofsInstrVCmd + 14);

      if (konamiAddrTuningTableHigh > konamiAddrTuningTableLow &&
          konamiAddrTuningTableHigh - konamiAddrTuningTableLow <= 0x7f) {
        konamiTuningTableAddress = konamiAddrTuningTableLow;
        konamiTuningTableSize = konamiAddrTuningTableHigh - konamiAddrTuningTableLow;
      }
    }
  }

  NinSnesScanResult scanResult;
  scanResult.signature = signature;
  scanResult.profile = profileId;
  scanResult.name = name;
  scanResult.songIndex = *guessedSongIndex;
  scanResult.songListAddr = addrSongList;
  scanResult.songStartAddr = addrSongStart;
  scanResult.sectionPtrAddr = addrSectionPtr;
  scanResult.instrTableAddr = addrInstrTable;
  scanResult.spcDirAddr = spcDirAddr;
  scanResult.konamiBaseAddress = konamiBaseAddress == 0xffff ? 0 : konamiBaseAddress;
  scanResult.falcomBaseOffset = falcomBaseOffset;
  scanResult.quintetBGMInstrBase = quintetBGMInstrBase;
  scanResult.quintetAddrBGMInstrLookup = quintetAddrBGMInstrLookup;
  scanResult.konamiTuningTableAddress = konamiTuningTableAddress;
  scanResult.konamiTuningTableSize = konamiTuningTableSize;
  scanResult.volumeTable = std::move(volumeTable);
  scanResult.durRateTable = std::move(durRateTable);

  loadFromScanResult(file, scanResult);
}
