/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NinSnesInstr.h"
#include "NinSnesSeq.h"
#include "ScannerManager.h"

#include <array>
#include <cstring>
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

template <size_t N>
bool matchNinSnesByteTable(RawFile* file, uint16_t offset, const std::array<uint8_t, N>& table) {
  return file->matchBytes(table.data(), offset, table.size());
}

template <size_t N>
BytePattern makePatchedBytePattern(const char (&bytes)[N], const char* mask,
                                   std::initializer_list<std::pair<size_t, uint8_t>> patches) {
  std::array<char, N - 1> pattern{};
  std::memcpy(pattern.data(), bytes, N - 1);
  for (const auto& [index, value] : patches) {
    pattern[index] = static_cast<char>(value);
  }
  return BytePattern(pattern.data(), mask, N - 1);
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
    // BEGIN DYNAMIC PATTERN DEFINITIONS

    //; Kirby Super Star SPC
    //; set initial value to section pointer
    // 08e4: f5 ff 38  mov   a,$38ff+x
    // 08e7: fd        mov   y,a
    // 08e8: f5 fe 38  mov   a,$38fe+x
    // 08eb: da 30     movw  $30,ya
    const BytePattern ptnInitSectionPtr = makePatchedBytePattern("\xf5\xff\x38\xfd\xf5\xfe\x38\xda"
                                                                 "\x30",
                                                                 "x??xx??x"
                                                                 "x",
                                                                 {{8, addrSectionPtr}});

    //; Yoshi's Island SPC
    //; set initial value to section pointer
    // 06f0: 1c        asl   a
    // 06f1: 5d        mov   x,a
    // 06f2: f5 8f ff  mov   a,$ff8f+x
    // 06f5: fd        mov   y,a
    // 06f6: d0 03     bne   $06fb
    // 06f8: c4 04     mov   $04,a
    // 06fa: 6f        ret
    // 06fb: f5 8e ff  mov   a,$ff8e+x
    // 06fe: da 40     movw  $40,ya
    const BytePattern ptnInitSectionPtrYI =
        makePatchedBytePattern("\x1c\x5d\xf5\x8f\xff\xfd\xd0\x03"
                               "\xc4\x04\x6f\xf5\x8e\xff\xda\x40",
                               "xxx??xxx"
                               "x?xx??xx",
                               {{15, addrSectionPtr}});

    //; Super Mario World SPC
    //; set initial value to section pointer
    // 0b5f: 1c        asl   a
    // 0b60: fd        mov   y,a
    // 0b61: f6 5e 13  mov   a,$135e+y
    // 0b64: c4 40     mov   $40,a
    // 0b66: f6 5f 13  mov   a,$135f+y
    // 0b69: c4 41     mov   $41,a
    const BytePattern ptnInitSectionPtrSMW = makePatchedBytePattern(
        "\x1c\xfd\xf6\x5e\x13\xc4\x40\xf6"
        "\x5f\x13\xc4\x41",
        "xxx??xxx"
        "??xx",
        {{6, addrSectionPtr}, {11, static_cast<uint8_t>(addrSectionPtr + 1)}});

    // DERIVED VERSIONS

    //; Gradius 3 SPC
    // 08b7: 5d        mov   x,a
    // 08b8: f5 fc 11  mov   a,$11fc+x
    // 08bb: f0 ee     beq   $08ab
    // 08bd: fd        mov   y,a
    // 08be: f5 fb 11  mov   a,$11fb+x
    // 08c1: da 40     movw  $40,ya            ; song metaindex ptr
    const BytePattern ptnInitSectionPtrGD3 =
        makePatchedBytePattern("\x5d\xf5\xfc\x11\xf0\xee\xfd\xf5"
                               "\xfb\x11\xda\x40",
                               "xx??x?xx"
                               "??xx",
                               {{11, addrSectionPtr}});

    //; Yoshi's Safari SPC
    // 1488: fd        mov   y,a
    // 1489: f7 48     mov   a,($48)+y
    // 148b: c4 4c     mov   $4c,a
    // 148d: fc        inc   y
    // 148e: f7 48     mov   a,($48)+y
    // 1490: c4 4d     mov   $4d,a
    const BytePattern ptnInitSectionPtrYSFR = makePatchedBytePattern(
        "\xfd\xf7\x48\xc4\x4c\xfc\xf7\x48"
        "\xc4\x4d",
        "xx?xxxx?"
        "xx",
        {{4, addrSectionPtr}, {9, static_cast<uint8_t>(addrSectionPtr + 1)}});

    //; Terranigma SPC
    // 0521: e5 fc 10  mov   a,$10fc
    // 0524: ec fd 10  mov   y,$10fd
    // 0527: da 23     movw  $23,ya            ; section list address from $10fc/d
    // 0529: 3a 23     incw  $23
    // 052b: 3a 23     incw  $23               ; skip first word
    const BytePattern ptnInitSectionPtrTS =
        makePatchedBytePattern("\xe5\xfc\x10\xec\xfd\x10\xda\x23"
                               "\x3a\x23\x3a\x23",
                               "x??x??xx"
                               "xxxx",
                               {{7, addrSectionPtr}, {9, addrSectionPtr}, {11, addrSectionPtr}});

    //; Ys IV - Mask of the Sun SPC
    // 11f8: f5 b9 06  mov   a,$06b9+x
    // 11fb: fd        mov   y,a
    // 11fc: f5 b8 06  mov   a,$06b8+x
    // 11ff: da 35     movw  $35,ya            ; load section playlist ptr
    // 1201: 2d        push  a
    // 1202: dd        mov   a,y
    // 1203: 80        setc
    // 1204: a8 d0     sbc   a,#$d0            ; address to offset (decrease $d000)
    // 1206: fd        mov   y,a
    // 1207: ae        pop   a
    // 1208: da 17     movw  $17,ya            ; save the offset
    const BytePattern ptnInitSectionPtrYs4 =
        makePatchedBytePattern("\xf5\xb9\x06\xfd\xf5\xb8\x06\xda"
                               "\x35\x2d\xdd\x80\xa8\xd0\xfd\xae"
                               "\xda\x17",
                               "x??xx??x"
                               "xxxxx?xx"
                               "xx",
                               {{8, addrSectionPtr}});

    // END DYNAMIC PATTERN DEFINITIONS

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
      const BytePattern ptnInitSongListPtrYSFR = makePatchedBytePattern(
          "\x8f\x00\x48\x8f\x1e\x49", "x?xx?x",
          {{2, addrSongListPtr}, {5, static_cast<uint8_t>(addrSongListPtr + 1)}});

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

  // GUESS SONG COUNT
  // Test Case: Star Fox, Kirby Super Star, Earthbound
  // Note that the result sometimes exceeds the real length
  uint8_t songListLength = 1;
  uint16_t addrSectionListCutoff = 0xffff;
  // skip index 0, it's not a part of table in most games
  for (uint8_t songIndex = 1; songIndex <= 0x7f; songIndex++) {
    uint32_t addrSectionListPtr = addrSongList + songIndex * 2;
    if (addrSectionListPtr >= addrSectionListCutoff) {
      break;
    }

    uint16_t firstSectionPtr = file->readShort(addrSectionListPtr);
    if (profile.addressModel == NinSnesAddressModelId::KonamiBase) {
      firstSectionPtr =
          convertNinSnesAddress(profile, firstSectionPtr, konamiBaseAddress, falcomBaseOffset);
    }
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
      // usually it does not appear
      // probably it's broken
      continue;
    }

    if (profile.addressModel == NinSnesAddressModelId::FalcomBaseOffset) {
      falcomBaseOffset = firstSectionPtr - falcomBaseAddress;
    }
    addrFirstSection =
        convertNinSnesAddress(profile, addrFirstSection, konamiBaseAddress, falcomBaseOffset);
    if (addrFirstSection + 16 > 0x10000) {
      break;
    }

    bool hasIllegalTrack = false;
    for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
      uint16_t addrTrackStart = file->readShort(addrFirstSection + trackIndex * 2);
      if (addrTrackStart != 0) {
        if (addrTrackStart == 0xffff) {
          hasIllegalTrack = true;
          break;
        }

        addrTrackStart =
            convertNinSnesAddress(profile, addrTrackStart, konamiBaseAddress, falcomBaseOffset);

        if ((addrTrackStart & 0xff00) == 0 || addrTrackStart == 0xffff) {
          hasIllegalTrack = true;
          break;
        }
      }
    }
    if (hasIllegalTrack) {
      break;
    }

    songListLength = songIndex + 1;
  }

  // GUESS CURRENT SONG NUMBER
  uint8_t guessedSongIndex = 0xff;

  // scan for a song that contains the current section
  // (note that the section pointer points to the "next" section actually, in most cases)
  uint16_t addrCurrentSection = file->readShort(addrSectionPtr);
  if (addrCurrentSection >= 0x0100 && addrCurrentSection < 0xfff0) {
    uint8_t songIndexCandidate = 0xff;

    for (uint8_t songIndex = 0; songIndex <= songListLength; songIndex++) {
      uint32_t addrSectionListPtr = addrSongList + songIndex * 2;
      if (addrSectionListPtr == 0 || addrSectionListPtr == 0xffff) {
        continue;
      }

      uint16_t firstSectionPtr = file->readShort(addrSectionListPtr);
      if (profile.addressModel == NinSnesAddressModelId::KonamiBase) {
        firstSectionPtr =
            convertNinSnesAddress(profile, firstSectionPtr, konamiBaseAddress, falcomBaseOffset);
      }
      if (profile.addressModel == NinSnesAddressModelId::FalcomBaseOffset) {
        falcomBaseOffset = firstSectionPtr - falcomBaseAddress;
      }
      if (firstSectionPtr > addrCurrentSection) {
        continue;
      }

      uint16_t curAddress = firstSectionPtr;
      if ((addrCurrentSection % 2) == (curAddress % 2)) {
        uint8_t sectionCount = 0;  // prevent overrun of illegal data
        while (curAddress >= 0x0100 && curAddress < 0xfff0 && sectionCount < 32) {
          uint16_t addrSection = convertNinSnesAddress(profile, file->readShort(curAddress),
                                                       konamiBaseAddress, falcomBaseOffset);

          if (curAddress == addrCurrentSection) {
            songIndexCandidate = songIndex;
            break;
          }

          if ((addrSection & 0xff00) == 0) {
            // section list end / jump
            break;
          }

          curAddress += 2;
          sectionCount++;
        }

        if (songIndexCandidate != 0xff) {
          break;
        }
      }
    }

    if (songIndexCandidate != 0xff) {
      guessedSongIndex = songIndexCandidate;
    }
  }

  if (guessedSongIndex == 0xff) {
    return;
  }

  // load the song
  uint16_t addrSongStart = file->readShort(addrSongList + guessedSongIndex * 2);
  if (profile.addressModel == NinSnesAddressModelId::KonamiBase) {
    addrSongStart =
        convertNinSnesAddress(profile, addrSongStart, konamiBaseAddress, falcomBaseOffset);
  }

  // scan for instrument table
  uint32_t ofsLoadInstrTableAddressASM;
  uint32_t addrInstrTable = 0;
  uint16_t spcDirAddr = 0;
  if (profile.id != NinSnesProfileId::Unknown) {
    if (file->searchBytePattern(ptnLoadInstrTableAddress, ofsLoadInstrTableAddressASM)) {
      addrInstrTable = file->readByte(ofsLoadInstrTableAddressASM + 7) |
                       (file->readByte(ofsLoadInstrTableAddressASM + 10) << 8);
      // Fix for HyperZone
      u32 firstWord = file->readWord(addrInstrTable);
      if (firstWord == 0 || firstWord == 0xFFFFFFFF) {
        addrInstrTable += 4;
      }
    } else if (file->searchBytePattern(ptnLoadInstrTableAddressSMW, ofsLoadInstrTableAddressASM)) {
      addrInstrTable = file->readByte(ofsLoadInstrTableAddressASM + 3) |
                       (file->readByte(ofsLoadInstrTableAddressASM + 6) << 8);
    } else {
      switch (profile.instrTableAddressModel) {
        case NinSnesInstrTableAddressModelId::Human:
          if (file->searchBytePattern(ptnLoadInstrTableAddressCTOW, ofsLoadInstrTableAddressASM)) {
            addrInstrTable = file->readByte(ofsLoadInstrTableAddressASM + 7) |
                             (file->readByte(ofsLoadInstrTableAddressASM + 10) << 8);
          } else if (file->searchBytePattern(ptnLoadInstrTableAddressSOS,
                                             ofsLoadInstrTableAddressASM)) {
            addrInstrTable = file->readByte(ofsLoadInstrTableAddressASM + 1) |
                             (file->readByte(ofsLoadInstrTableAddressASM + 4) << 8);
          } else {
            return;
          }
          break;

        case NinSnesInstrTableAddressModelId::Tose:
          if (file->searchBytePattern(ptnLoadInstrTableAddressYSFR, ofsLoadInstrTableAddressASM)) {
            spcDirAddr = file->readByte(ofsLoadInstrTableAddressASM + 3) << 8;
            addrInstrTable = file->readByte(ofsLoadInstrTableAddressASM + 10) |
                             (file->readByte(ofsLoadInstrTableAddressASM + 13) << 8);
          } else {
            return;
          }
          break;

        case NinSnesInstrTableAddressModelId::Standard:
        default:
          return;
      }
    }

    // scan for DIR address
    if (spcDirAddr == 0) {
      uint32_t ofsSetDIR;
      if (file->searchBytePattern(ptnSetDIR, ofsSetDIR)) {
        spcDirAddr = file->readByte(ofsSetDIR + 4) << 8;
      } else if (file->searchBytePattern(ptnSetDIRYI, ofsSetDIR)) {
        spcDirAddr = file->readByte(ofsSetDIR + 1) << 8;
      } else if (file->searchBytePattern(ptnSetDIRVS, ofsSetDIR)) {
        u16 spcDirAddrPtr = file->readShort(ofsSetDIR + 1);
        spcDirAddr = file->readByte(spcDirAddrPtr) << 8;
      } else if (file->searchBytePattern(ptnSetDIRSMW, ofsSetDIR)) {
        spcDirAddr = file->readByte(ofsSetDIR + 9) << 8;
      }
      // DERIVED VERSIONS
      else if (file->searchBytePattern(ptnSetDIRCTOW, ofsSetDIR)) {
        spcDirAddr = file->readByte(ofsSetDIR + 3) << 8;
      } else if (file->searchBytePattern(ptnSetDIRTS, ofsSetDIR)) {
        spcDirAddr = file->readByte(ofsSetDIR + 1) << 8;
      } else {
        return;
      }
    }
  }

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
  scanResult.songIndex = guessedSongIndex;
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
