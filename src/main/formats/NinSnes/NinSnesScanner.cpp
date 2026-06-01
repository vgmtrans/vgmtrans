/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "base/Types.h"
#include "NinSnesInstr.h"
#include "NinSnesSeq.h"
#include "ScannerManager.h"
#include "SNESDSP.h"

#include <algorithm>
#include <array>
#include <optional>

namespace vgmtrans::scanners {
ScannerRegistration<NinSnesScanner> s_nin_snes("NinSnes", {"spc"});
}

namespace {

struct NinSnesSongListInfo {
  u32 address = 0;
  NinSnesSignatureId signature = NinSnesSignatureId::None;
  NinSnesProfileId profileId = NinSnesProfileId::Unknown;
  u16 konamiBaseAddress = 0xffff;
  u16 falcomBaseAddress = 0xffff;
};

struct NinSnesVoiceCommandInfo {
  u8 firstVoiceCmd = 0;
  u16 addressTable = 0;
  u16 lengthTable = 0;
  u8 numCommands = 0;
  NinSnesSignatureId signature = NinSnesSignatureId::None;
  NinSnesProfileId profileId = NinSnesProfileId::Unknown;
};

struct NinSnesInstrumentProbeInfo {
  u32 tableAddress = 0;
  u16 dirAddress = 0;
};

struct NinSnesAddmusicKCustomInstrInfo {
  u32 tableAddress = 0;
  u8 baseInstrument = 0;
  u8 count = 0;
};

struct NinSnesDirProbeInfo {
  u16 address = 0;
  u8 score = 0;
};

template <size_t N>
bool matchNinSnesByteTable(RawFile* file, u16 offset, const std::array<u8, N>& table) {
  return file->matchBytes(table.data(), offset, table.size());
}

std::vector<u8> readNinSnesByteTable(RawFile* file, u16 address, u8 length) {
  std::vector<u8> table;
  table.reserve(length);
  for (u8 offset = 0; offset < length; offset++) {
    table.push_back(file->readByte(address + offset));
  }
  return table;
}

u8 countNinSnesVoiceCommands(u8 firstVoiceCmd, u16 addressTable, u16 lengthTable) {
  if (addressTable < lengthTable && addressTable + (0x100 - firstVoiceCmd) * 2 >= lengthTable) {
    return static_cast<u8>((lengthTable - addressTable) / 2);
  }
  return 0;
}

NinSnesDirProbeInfo scoreNinSnesDirAddress(RawFile* file, NinSnesProfileId profileId, u32 instrTableAddress,
                                           u16 dirAddress) {
  const auto& profile = getNinSnesProfile(profileId);
  const u32 instrItemSize = NinSnesInstr::expectedSize(profileId);
  NinSnesDirProbeInfo info{dirAddress, 0};

  for (u8 instrIndex = 0; instrIndex < 32; instrIndex++) {
    const u32 addrInstrHeader = instrTableAddress + instrItemSize * instrIndex;
    if (addrInstrHeader + instrItemSize > 0x10000) {
      break;
    }

    const u8 srcn = file->readByte(addrInstrHeader);
    const u8 adsr1 = file->readByte(addrInstrHeader + 1);
    const u8 gain = file->readByte(addrInstrHeader + 3);
    if (srcn >= 0x80 || (adsr1 == 0 && gain == 0)) {
      continue;
    }

    if (NinSnesInstr::isValidHeader(file, profileId, addrInstrHeader, dirAddress, true)) {
      if (requiresNinSnesSampleStartAfterDirEntry(profile)) {
        const u32 addrDirEntry = dirAddress + srcn * 4;
        if (file->readShort(addrDirEntry) < addrDirEntry + 4) {
          continue;
        }
      }
      info.score++;
    }
  }

  return info;
}

std::optional<NinSnesDirProbeInfo> inferNinSnesDirAddress(RawFile* file, NinSnesProfileId profileId,
                                                          u32 instrTableAddress) {
  NinSnesDirProbeInfo best;
  for (u32 dirAddress = 0; dirAddress < 0x10000; dirAddress += 0x100) {
    const auto candidate = scoreNinSnesDirAddress(file, profileId, instrTableAddress, static_cast<u16>(dirAddress));
    if (candidate.score > best.score) {
      best = candidate;
    }
  }

  if (best.score < 4) {
    return std::nullopt;
  }
  return best;
}

constexpr std::array<u8, 27> kStandardVcmdLengthTable = {
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01,
    0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01,
};

constexpr std::array<u8, 40> kIntelliFe3VcmdLengthTable = {
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01,
    0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x02, 0x02,
};

constexpr std::array<u8, 36> kIntelliFe4VcmdLengthTable = {
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01, 0x02, 0x03, 0x01, 0x03,
    0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01, 0x00, 0x00, 0x02, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01,
};

constexpr std::array<u8, 36> kIntelliTaVcmdLengthTable = {
    0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01, 0x02, 0x03, 0x01, 0x03,
    0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01, 0x00, 0x00, 0x02, 0x02, 0x00, 0x01, 0x01, 0x01, 0x01,
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

bool NinSnesScanner::loadFromScanResult(RawFile* file, const NinSnesScanResult& scanResult,
                                        bool loadInstrumentSet) {
  auto* newSeq = new NinSnesSeq(file, scanResult);
  if (!newSeq->loadVGMFile()) {
    delete newSeq;
    return false;
  }

  if (!loadInstrumentSet || scanResult.profile == NinSnesProfileId::Unknown ||
      scanResult.instrTableAddr == 0 || scanResult.spcDirAddr == 0) {
    return true;
  }

  auto* newInstrSet = new NinSnesInstrSet(file, scanResult);
  if (!newInstrSet->loadVGMFile()) {
    delete newInstrSet;
    return true;
  }

  return true;
}

void NinSnesScanner::searchForNinSnesFromARAM(RawFile* file) {
  NinSnesProfileId profileId = NinSnesProfileId::Unknown;
  NinSnesSignatureId signature = NinSnesSignatureId::None;

  std::string basefilename = file->stem();
  std::string name = file->tag.hasTitle() ? file->tag.title : basefilename;

  // get section pointer address
  u32 ofsIncSectionPtr;
  u8 addrSectionPtr;
  u16 konamiBaseAddress = 0xffff;
  u16 falcomBaseAddress = 0xffff;
  u16 falcomBaseOffset = 0;
  if (file->searchBytePattern(ptnIncSectionPtr, ofsIncSectionPtr)) {
    signature = NinSnesSignatureId::Standard;
    addrSectionPtr = file->readByte(ofsIncSectionPtr + 3);
  }
  // DERIVED VERSIONS
  else if (file->searchBytePattern(ptnIncSectionPtrGD3, ofsIncSectionPtr)) {
    signature = NinSnesSignatureId::Konami;
    u8 konamiBaseAddressPtr = file->readByte(ofsIncSectionPtr + 16);
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
    const BytePattern ptnInitSectionPtrSMWAddmusicK = makeInitSectionPtrSMWAddmusicKPattern(addrSectionPtr);
    const BytePattern ptnInitSectionPtrGD3 = makeInitSectionPtrGD3Pattern(addrSectionPtr);
    const BytePattern ptnInitSectionPtrYSFR = makeInitSectionPtrYSFRPattern(addrSectionPtr);
    const BytePattern ptnInitSectionPtrTS = makeInitSectionPtrTSPattern(addrSectionPtr);
    const BytePattern ptnInitSectionPtrYs4 = makeInitSectionPtrYs4Pattern(addrSectionPtr);

    u32 ofsInitSectionPtr;
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

    if (file->searchBytePattern(ptnInitSectionPtrSMWAddmusicK, ofsInitSectionPtr)) {
      info.signature = NinSnesSignatureId::Earlier;
      info.profileId = NinSnesProfileId::AddmusicK;
      // AddmusicK keeps a dummy word before the real song pointer list.
      info.address = file->readShort(ofsInitSectionPtr + 3) + 2;
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
      const u16 addrSongListPtr = file->readShort(ofsInitSectionPtr + 1);
      info.address = file->readShort(addrSongListPtr);
      return info;
    }

    if (file->searchBytePattern(ptnInitSectionPtrHE4, ofsInitSectionPtr)) {
      info.address = file->readByte(ofsInitSectionPtr + 4) << 8;
      return info;
    }

    if (file->searchBytePattern(ptnInitSectionPtrYSFR, ofsInitSectionPtr)) {
      const u8 addrSongListPtr = file->readByte(ofsInitSectionPtr + 2);
      const BytePattern ptnInitSongListPtrYSFR = makeInitSongListPtrYSFRPattern(addrSongListPtr);

      u32 ofsInitSongListPtr;
      if (!file->searchBytePattern(ptnInitSongListPtrYSFR, ofsInitSongListPtr)) {
        return std::nullopt;
      }

      info.address = file->readByte(ofsInitSongListPtr + 1) | (file->readByte(ofsInitSongListPtr + 4) << 8);
      return info;
    }

    return std::nullopt;
  };

  const auto songListInfo = findSongList();
  if (!songListInfo) {
    return;
  }

  const u32 addrSongList = songListInfo->address;
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
    u32 ofsBranchForVcmd;
    NinSnesVoiceCommandInfo info;

    if (file->searchBytePattern(ptnJumpToVcmdYSFR, ofsBranchForVcmd)) {
      info.addressTable = file->readShort(ofsBranchForVcmd + 9);

      u32 ofsReadVcmdLength;
      if (!file->searchBytePattern(ptnReadVcmdLengthYSFR, ofsReadVcmdLength)) {
        return std::nullopt;
      }

      info.firstVoiceCmd = file->readByte(ofsReadVcmdLength + 2);
      info.lengthTable = file->readShort(ofsReadVcmdLength + 7);
      info.profileId = info.firstVoiceCmd == 0xe0 ? NinSnesProfileId::Tose : NinSnesProfileId::Unknown;
      info.numCommands = countNinSnesVoiceCommands(info.firstVoiceCmd, info.addressTable, info.lengthTable);
      return info;
    }

    if (file->searchBytePattern(ptnJumpToVcmdYs4, ofsBranchForVcmd)) {
      info.addressTable = file->readShort(ofsBranchForVcmd + 10);

      u32 ofsReadVcmdLength;
      if (!file->searchBytePattern(ptnReadVcmdLengthYs4, ofsReadVcmdLength)) {
        return std::nullopt;
      }

      info.firstVoiceCmd = file->readByte(ofsReadVcmdLength + 1);
      info.lengthTable = file->readShort(ofsReadVcmdLength + 9) + info.firstVoiceCmd;
      info.numCommands = countNinSnesVoiceCommands(info.firstVoiceCmd, info.addressTable, info.lengthTable);
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

    u32 ofsJumpToVcmd;
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
      u32 ofsReadVcmdLength;
      if (file->searchBytePattern(ptnReadVcmdLengthSMW, ofsReadVcmdLength)) {
        info.lengthTable = file->readShort(ofsReadVcmdLength + 9) + info.firstVoiceCmd;
      } else if (file->searchBytePattern(ptnReadVcmdLengthSMWAddmusicK, ofsReadVcmdLength)) {
        info.profileId = NinSnesProfileId::AddmusicK;
        info.lengthTable = file->readShort(ofsReadVcmdLength + 35) + info.firstVoiceCmd;
      } else {
        return std::nullopt;
      }

      if (info.profileId == NinSnesProfileId::Unknown) {
        info.profileId = NinSnesProfileId::Earlier;
      }
      info.addressTable = file->readShort(ofsJumpToVcmd + 5) + ((info.firstVoiceCmd * 2) & 0xff);
    } else {
      return std::nullopt;
    }

    info.numCommands = countNinSnesVoiceCommands(info.firstVoiceCmd, info.addressTable, info.lengthTable);
    return info;
  };

  const auto voiceCommandInfo = findVoiceCommands();
  if (!voiceCommandInfo) {
    return;
  }

  const u8 firstVoiceCmd = voiceCommandInfo->firstVoiceCmd;
  const u16 addrVoiceCmdAddressTable = voiceCommandInfo->addressTable;
  const u16 addrVoiceCmdLengthTable = voiceCommandInfo->lengthTable;
  const u8 numOfVoiceCmd = voiceCommandInfo->numCommands;
  if (voiceCommandInfo->signature != NinSnesSignatureId::None) {
    signature = voiceCommandInfo->signature;
  }
  if (voiceCommandInfo->profileId != NinSnesProfileId::Unknown) {
    profileId = voiceCommandInfo->profileId;
  }

  // TRY TO GRAB NOTE VOLUME/DURATION TABLE
  u32 ofsDispatchNote = 0;
  u16 addrDurRateTable = 0;
  u16 addrVolumeTable = 0;
  std::vector<u8> durRateTable;
  std::vector<u8> volumeTable;
  auto loadNoteTable = [&](const BytePattern& pattern, u8 durTableOffset, u8 volumeTableOffset) -> bool {
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
  u32 ofsInstrVCmd = 0;
  auto classifyIntelligentProfile = [&]() -> NinSnesProfileId {
    u32 ofsIntelliVCmdFA;
    if (!file->searchBytePattern(ptnIntelliVCmdFA, ofsIntelliVCmdFA)) {
      return NinSnesProfileId::Unknown;
    }

    if (file->searchBytePattern(ptnDispatchNoteFE3, ofsDispatchNote)) {
      return firstVoiceCmd == 0xd6 && matchNinSnesByteTable(file, addrVoiceCmdLengthTable, kIntelliFe3VcmdLengthTable)
                 ? NinSnesProfileId::IntelliFe3
                 : NinSnesProfileId::Unknown;
    }

    if (file->searchBytePattern(ptnDispatchNoteFE4, ofsDispatchNote)) {
      return firstVoiceCmd == 0xda && matchNinSnesByteTable(file, addrVoiceCmdLengthTable, kIntelliFe4VcmdLengthTable)
                 ? NinSnesProfileId::IntelliFe4
                 : NinSnesProfileId::Unknown;
    }

    return firstVoiceCmd == 0xda && matchNinSnesByteTable(file, addrVoiceCmdLengthTable, kIntelliTaVcmdLengthTable)
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

    if (firstVoiceCmd != 0xe0 || !matchNinSnesByteTable(file, addrVoiceCmdLengthTable, kStandardVcmdLengthTable)) {
      return classifyIntelligentProfile();
    }

    const bool canonicalVcmdLayout =
        addrVoiceCmdAddressTable + (kStandardVcmdLengthTable.size() * 2) == addrVoiceCmdLengthTable;
    if (canonicalVcmdLayout) {
      u32 ofsWriteVolume;

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

    u32 ofsRD1VCmd_FA_FE;
    u32 ofsRD2VCmdInstrADSR;
    const bool hasQuintetLookupTail = numOfVoiceCmd == 32 && file->readByte(addrVoiceCmdLengthTable + 31) == 1;

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
  u8 quintetBGMInstrBase = 0;
  u16 quintetAddrBGMInstrLookup = 0;
  switch (profile.programResolver) {
    case NinSnesProgramResolverId::QuintetActRBase: {
      signature = NinSnesSignatureId::Quintet;
      u16 addrBGMInstrBase = file->readShort(ofsInstrVCmd + 18);
      quintetBGMInstrBase = file->readByte(addrBGMInstrBase);
      break;
    }

    case NinSnesProgramResolverId::QuintetLookup:
      signature = NinSnesSignatureId::Quintet;
      quintetAddrBGMInstrLookup = file->readShort(ofsInstrVCmd + (profile.id == NinSnesProfileId::QuintetTs ? 18 : 19));
      break;

    case NinSnesProgramResolverId::Direct:
    case NinSnesProgramResolverId::StandardPercussion:
    case NinSnesProgramResolverId::IntelliTaOverride:
    default:
      break;
  }

  auto readSectionListPtr = [&](u32 addrSectionListPtr) -> u16 {
    u16 firstSectionPtr = file->readShort(addrSectionListPtr);
    if (profile.addressModel == NinSnesAddressModelId::KonamiBase) {
      firstSectionPtr = convertNinSnesAddress(profile, firstSectionPtr, konamiBaseAddress, falcomBaseOffset);
    }
    return firstSectionPtr;
  };

  auto updateFalcomBaseOffset = [&](u16 firstSectionPtr) {
    if (profile.addressModel == NinSnesAddressModelId::FalcomBaseOffset) {
      falcomBaseOffset = firstSectionPtr - falcomBaseAddress;
    }
  };

  auto hasIllegalTrackPointers = [&](u16 addrFirstSection) -> bool {
    for (u8 trackIndex = 0; trackIndex < 8; trackIndex++) {
      u16 addrTrackStart = file->readShort(addrFirstSection + trackIndex * 2);
      if (addrTrackStart == 0) {
        continue;
      }
      if (addrTrackStart == 0xffff) {
        return true;
      }

      addrTrackStart = convertNinSnesAddress(profile, addrTrackStart, konamiBaseAddress, falcomBaseOffset);
      if ((addrTrackStart & 0xff00) == 0 || addrTrackStart == 0xffff) {
        return true;
      }
    }
    return false;
  };

  auto findSongListLength = [&]() -> u8 {
    u8 songListLength = 1;
    u16 addrSectionListCutoff = 0xffff;
    if (profile.id == NinSnesProfileId::AddmusicK) {
      const u16 firstSectionListPtr = readSectionListPtr(addrSongList);
      if (firstSectionListPtr >= 0x0100 && firstSectionListPtr < 0xfff0) {
        addrSectionListCutoff = firstSectionListPtr;
      }
    }

    for (u8 songIndex = 1; songIndex <= 0x7f; songIndex++) {
      const u32 addrSectionListPtr = addrSongList + songIndex * 2;
      if (addrSectionListPtr >= addrSectionListCutoff) {
        break;
      }

      const u16 firstSectionPtr = readSectionListPtr(addrSectionListPtr);
      if (firstSectionPtr == 0) {
        continue;
      }
      if ((firstSectionPtr & 0xff00) == 0 || firstSectionPtr == 0xffff) {
        break;
      }
      if (firstSectionPtr >= addrSectionListPtr) {
        addrSectionListCutoff = std::min(addrSectionListCutoff, firstSectionPtr);
      }

      u16 addrFirstSection = file->readShort(firstSectionPtr);
      if (addrFirstSection < 0x0100) {
        continue;
      }

      updateFalcomBaseOffset(firstSectionPtr);
      addrFirstSection = convertNinSnesAddress(profile, addrFirstSection, konamiBaseAddress, falcomBaseOffset);
      if (addrFirstSection + 16 > 0x10000 || hasIllegalTrackPointers(addrFirstSection)) {
        break;
      }

      songListLength = songIndex + 1;
    }

    return songListLength;
  };

  auto findCurrentSongIndex = [&](u8 songListLength) -> std::optional<u8> {
    const u16 addrCurrentSection = file->readShort(addrSectionPtr);
    if (addrCurrentSection < 0x0100 || addrCurrentSection >= 0xfff0) {
      return std::nullopt;
    }

    for (u8 songIndex = 0; songIndex <= songListLength; songIndex++) {
      const u32 addrSectionListPtr = addrSongList + songIndex * 2;
      if (addrSectionListPtr == 0 || addrSectionListPtr == 0xffff) {
        continue;
      }

      const u16 firstSectionPtr = readSectionListPtr(addrSectionListPtr);
      updateFalcomBaseOffset(firstSectionPtr);
      if (firstSectionPtr > addrCurrentSection || (addrCurrentSection % 2) != (firstSectionPtr % 2)) {
        continue;
      }

      u16 curAddress = firstSectionPtr;
      u8 sectionCount = 0;
      while (curAddress >= 0x0100 && curAddress < 0xfff0 && sectionCount < 32) {
        const u16 addrSection = readNinSnesAddress(profile, file, curAddress, konamiBaseAddress, falcomBaseOffset);
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

  auto scoreFallbackSongIndex = [&](u8 songIndex) -> std::optional<u32> {
    const u16 firstSectionPtr = readSectionListPtr(addrSongList + songIndex * 2);
    if (firstSectionPtr < 0x0100 || firstSectionPtr >= 0xfff0) {
      return std::nullopt;
    }

    updateFalcomBaseOffset(firstSectionPtr);
    u16 addrFirstSection =
        convertNinSnesAddress(profile, file->readShort(firstSectionPtr), konamiBaseAddress, falcomBaseOffset);
    if (addrFirstSection + 16 > 0x10000 || hasIllegalTrackPointers(addrFirstSection)) {
      return std::nullopt;
    }

    u8 trackCount = 0;
    u16 minTrackStart = 0xffff;
    u16 maxTrackStart = 0;
    for (u8 trackIndex = 0; trackIndex < 8; trackIndex++) {
      const u16 addrTrackStart =
          convertNinSnesAddress(profile, file->readShort(addrFirstSection + trackIndex * 2), konamiBaseAddress,
                                falcomBaseOffset);
      if (addrTrackStart < 0x0100 || addrTrackStart >= 0xfff0) {
        continue;
      }

      trackCount++;
      minTrackStart = std::min(minTrackStart, addrTrackStart);
      maxTrackStart = std::max(maxTrackStart, addrTrackStart);
    }

    if (trackCount == 0) {
      return std::nullopt;
    }

    return (static_cast<u32>(trackCount) << 16) + (maxTrackStart - minTrackStart);
  };

  auto findFallbackSongIndex = [&](u8 songListLength) -> std::optional<u8> {
    std::optional<u8> bestSongIndex;
    u32 bestScore = 0;

    for (u8 songIndex = 0; songIndex < songListLength; songIndex++) {
      const auto score = scoreFallbackSongIndex(songIndex);
      if (!score) {
        continue;
      }

      if (profile.id != NinSnesProfileId::AddmusicK) {
        return songIndex;
      }
      if (!bestSongIndex || *score > bestScore) {
        bestSongIndex = songIndex;
        bestScore = *score;
      }
    }

    return bestSongIndex;
  };

  auto findValidSongIndexes = [&](u8 songListLength) -> std::vector<u8> {
    std::vector<u8> songIndexes;
    for (u8 songIndex = 0; songIndex < songListLength; songIndex++) {
      if (scoreFallbackSongIndex(songIndex)) {
        songIndexes.push_back(songIndex);
      }
    }
    return songIndexes;
  };

  auto findAddmusicKCustomInstrInfo = [&](u16 songStartAddr,
                                          u16 spcDirAddr) -> std::optional<NinSnesAddmusicKCustomInstrInfo> {
    if (profile.id != NinSnesProfileId::AddmusicK || songStartAddr < 0x0100 || songStartAddr >= 0xfff0) {
      return std::nullopt;
    }

    u32 curOffset = songStartAddr;
    u16 firstSectionAddr = 0xffff;
    for (u8 sectionIndex = 0; sectionIndex < 32; sectionIndex++) {
      if (curOffset + 2 > 0x10000) {
        return std::nullopt;
      }

      const u16 sectionAddress = readNinSnesAddress(profile, file, curOffset, konamiBaseAddress, falcomBaseOffset);
      curOffset += 2;
      if (sectionAddress == 0) {
        break;
      }
      if (sectionAddress <= 0xff) {
        if (curOffset + 2 > 0x10000) {
          return std::nullopt;
        }
        curOffset += 2;
        break;
      }
      if (sectionAddress < 0xfff0) {
        firstSectionAddr = std::min(firstSectionAddr, sectionAddress);
      }
    }

    if (firstSectionAddr == 0xffff || firstSectionAddr <= curOffset ||
        ((firstSectionAddr - curOffset) % 6) != 0) {
      return std::nullopt;
    }

    const u32 entryCount = (firstSectionAddr - curOffset) / 6;
    if (entryCount == 0 || entryCount > 0x40) {
      return std::nullopt;
    }

    for (u32 entryIndex = 0; entryIndex < entryCount; entryIndex++) {
      const u32 entryOffset = curOffset + entryIndex * 6;
      const u8 srcn = file->readByte(entryOffset);
      if (srcn >= 0x80) {
        return std::nullopt;
      }

      const u32 dirEntryOffset = spcDirAddr + srcn * 4;
      if (dirEntryOffset + 4 > 0x10000 || !SNESSampColl::isValidSampleDir(file, dirEntryOffset, true)) {
        return std::nullopt;
      }
      if (requiresNinSnesSampleStartAfterDirEntry(profile) && file->readShort(dirEntryOffset) < dirEntryOffset + 4) {
        return std::nullopt;
      }
    }

    return NinSnesAddmusicKCustomInstrInfo{
        .tableAddress = curOffset,
        .baseInstrument = 0x1e,
        .count = static_cast<u8>(entryCount),
    };
  };

  const u8 songListLength = findSongListLength();
  std::vector<u8> songIndexesToLoad;
  if (profile.id == NinSnesProfileId::AddmusicK) {
    songIndexesToLoad = findValidSongIndexes(songListLength);
  } else {
    auto guessedSongIndex = findCurrentSongIndex(songListLength);
    if (!guessedSongIndex) {
      guessedSongIndex = findFallbackSongIndex(songListLength);
    }
    if (guessedSongIndex) {
      songIndexesToLoad.push_back(*guessedSongIndex);
    }
  }
  if (songIndexesToLoad.empty()) {
    return;
  }

  auto findDirAddress = [&]() -> std::optional<u16> {
    u32 ofsSetDIR;
    if (file->searchBytePattern(ptnSetDIR, ofsSetDIR)) {
      return static_cast<u16>(file->readByte(ofsSetDIR + 4) << 8);
    }
    if (file->searchBytePattern(ptnSetDIRYI, ofsSetDIR)) {
      return static_cast<u16>(file->readByte(ofsSetDIR + 1) << 8);
    }
    if (file->searchBytePattern(ptnSetDIRVS, ofsSetDIR)) {
      const u16 spcDirAddrPtr = file->readShort(ofsSetDIR + 1);
      return static_cast<u16>(file->readByte(spcDirAddrPtr) << 8);
    }
    if (file->searchBytePattern(ptnSetDIRSMW, ofsSetDIR)) {
      return static_cast<u16>(file->readByte(ofsSetDIR + 9) << 8);
    }
    if (file->searchBytePattern(ptnSetDIRCTOW, ofsSetDIR)) {
      return static_cast<u16>(file->readByte(ofsSetDIR + 3) << 8);
    }
    if (file->searchBytePattern(ptnSetDIRTS, ofsSetDIR)) {
      return static_cast<u16>(file->readByte(ofsSetDIR + 1) << 8);
    }
    return std::nullopt;
  };

  auto findInstrumentProbeInfo = [&]() -> std::optional<NinSnesInstrumentProbeInfo> {
    if (profile.id == NinSnesProfileId::Unknown) {
      return NinSnesInstrumentProbeInfo{};
    }

    u32 ofsLoadInstrTableAddressASM;
    NinSnesInstrumentProbeInfo info;
    if (file->searchBytePattern(ptnLoadInstrTableAddress, ofsLoadInstrTableAddressASM)) {
      info.tableAddress =
          file->readByte(ofsLoadInstrTableAddressASM + 7) | (file->readByte(ofsLoadInstrTableAddressASM + 10) << 8);
      const u32 firstWord = file->readWord(info.tableAddress);
      if (firstWord == 0 || firstWord == 0xFFFFFFFF) {
        info.tableAddress += 4;
      }
    } else if (file->searchBytePattern(ptnLoadInstrTableAddressSMW, ofsLoadInstrTableAddressASM)) {
      info.tableAddress =
          file->readByte(ofsLoadInstrTableAddressASM + 3) | (file->readByte(ofsLoadInstrTableAddressASM + 6) << 8);
    } else if (file->searchBytePattern(ptnLoadInstrTableAddressSMWAddmusicK, ofsLoadInstrTableAddressASM)) {
      info.tableAddress =
          file->readByte(ofsLoadInstrTableAddressASM + 1) | (file->readByte(ofsLoadInstrTableAddressASM + 4) << 8);
    } else {
      switch (profile.instrTableAddressModel) {
        case NinSnesInstrTableAddressModelId::Human:
          if (file->searchBytePattern(ptnLoadInstrTableAddressCTOW, ofsLoadInstrTableAddressASM)) {
            info.tableAddress = file->readByte(ofsLoadInstrTableAddressASM + 7) |
                                (file->readByte(ofsLoadInstrTableAddressASM + 10) << 8);
          } else if (file->searchBytePattern(ptnLoadInstrTableAddressSOS, ofsLoadInstrTableAddressASM)) {
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
        const auto inferredDirAddress = inferNinSnesDirAddress(file, profile.id, info.tableAddress);
        if (!inferredDirAddress) {
          return std::nullopt;
        }
        info.dirAddress = inferredDirAddress->address;
        return info;
      }
      info.dirAddress = *dirAddress;
    }

    if (profile.id == NinSnesProfileId::AddmusicK) {
      const auto currentDirScore = scoreNinSnesDirAddress(file, profile.id, info.tableAddress, info.dirAddress);
      const auto inferredDirAddress = inferNinSnesDirAddress(file, profile.id, info.tableAddress);
      if (inferredDirAddress && inferredDirAddress->score > currentDirScore.score) {
        info.dirAddress = inferredDirAddress->address;
      }
    }

    return info;
  };

  u32 addrInstrTable = 0;
  u16 spcDirAddr = 0;
  const auto instrumentProbeInfo = findInstrumentProbeInfo();
  if (!instrumentProbeInfo) {
    return;
  }
  addrInstrTable = instrumentProbeInfo->tableAddress;
  spcDirAddr = instrumentProbeInfo->dirAddress;

  u16 konamiTuningTableAddress = 0;
  u8 konamiTuningTableSize = 0;
  if (profile.instrumentLayout == NinSnesInstrumentLayoutId::KonamiTuningTable) {
    if (file->searchBytePattern(ptnInstrVCmdGD3, ofsInstrVCmd)) {
      u16 konamiAddrTuningTableLow = file->readShort(ofsInstrVCmd + 10);
      u16 konamiAddrTuningTableHigh = file->readShort(ofsInstrVCmd + 14);

      if (konamiAddrTuningTableHigh > konamiAddrTuningTableLow &&
          konamiAddrTuningTableHigh - konamiAddrTuningTableLow <= 0x7f) {
        konamiTuningTableAddress = konamiAddrTuningTableLow;
        konamiTuningTableSize = konamiAddrTuningTableHigh - konamiAddrTuningTableLow;
      }
    }
  }

  NinSnesScanResult baseScanResult;
  baseScanResult.signature = signature;
  baseScanResult.profile = profileId;
  baseScanResult.songListAddr = addrSongList;
  baseScanResult.sectionPtrAddr = addrSectionPtr;
  baseScanResult.instrTableAddr = addrInstrTable;
  baseScanResult.spcDirAddr = spcDirAddr;
  baseScanResult.konamiBaseAddress = konamiBaseAddress == 0xffff ? 0 : konamiBaseAddress;
  baseScanResult.falcomBaseOffset = falcomBaseOffset;
  baseScanResult.quintetBGMInstrBase = quintetBGMInstrBase;
  baseScanResult.quintetAddrBGMInstrLookup = quintetAddrBGMInstrLookup;
  baseScanResult.konamiTuningTableAddress = konamiTuningTableAddress;
  baseScanResult.konamiTuningTableSize = konamiTuningTableSize;
  baseScanResult.volumeTable = volumeTable;
  baseScanResult.durRateTable = durRateTable;

  const bool appendSongIndex = profile.id == NinSnesProfileId::AddmusicK || songIndexesToLoad.size() > 1;
  std::vector<NinSnesScanResult> scanResults;
  for (const u8 songIndex : songIndexesToLoad) {
    NinSnesScanResult scanResult = baseScanResult;
    scanResult.name = appendSongIndex ? name + " (index " + std::to_string(songIndex) + ")" : name;
    scanResult.songIndex = songIndex;
    scanResult.songStartAddr = file->readShort(addrSongList + songIndex * 2);
    if (profile.addressModel == NinSnesAddressModelId::KonamiBase) {
      scanResult.songStartAddr =
          convertNinSnesAddress(profile, scanResult.songStartAddr, konamiBaseAddress, falcomBaseOffset);
    }

    if (const auto customInstrInfo =
            findAddmusicKCustomInstrInfo(static_cast<u16>(scanResult.songStartAddr), scanResult.spcDirAddr)) {
      scanResult.addmusicKCustomInstrTableAddr = customInstrInfo->tableAddress;
      scanResult.addmusicKCustomInstrBase = customInstrInfo->baseInstrument;
      scanResult.addmusicKCustomInstrCount = customInstrInfo->count;
    }

    scanResults.push_back(scanResult);
  }

  std::stable_sort(scanResults.begin(), scanResults.end(), [](const auto& lhs, const auto& rhs) {
    const bool lhsHasCustomInstruments = lhs.addmusicKCustomInstrCount != 0;
    const bool rhsHasCustomInstruments = rhs.addmusicKCustomInstrCount != 0;
    if (lhsHasCustomInstruments != rhsHasCustomInstruments) {
      return lhsHasCustomInstruments;
    }
    return lhs.songStartAddr > rhs.songStartAddr;
  });

  bool loadedInstrumentSet = false;
  for (const auto& scanResult : scanResults) {
    const bool loadInstrumentSet = scanResult.addmusicKCustomInstrCount != 0 || !loadedInstrumentSet;
    if (loadFromScanResult(file, scanResult, loadInstrumentSet) && scanResult.addmusicKCustomInstrCount == 0) {
      loadedInstrumentSet = true;
    }
  }
}
