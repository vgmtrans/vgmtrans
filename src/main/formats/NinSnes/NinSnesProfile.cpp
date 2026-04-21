#include "NinSnesProfile.h"

#include <array>
#include <algorithm>
#include <utility>

#include "SNESDSP.h"
#include "io/RawFile.h"

namespace {

template <size_t N>
std::vector<uint8_t> defaultTable(const std::array<uint8_t, N>& values) {
  return {values.begin(), values.end()};
}

void loadStandardVcmdMap(std::map<uint8_t, NinSnesSeqEventType>& eventMap, uint8_t statusByte) {
  eventMap[statusByte + 0x00] = EVENT_PROGCHANGE;
  eventMap[statusByte + 0x01] = EVENT_PAN;
  eventMap[statusByte + 0x02] = EVENT_PAN_FADE;
  eventMap[statusByte + 0x03] = EVENT_VIBRATO_ON;
  eventMap[statusByte + 0x04] = EVENT_VIBRATO_OFF;
  eventMap[statusByte + 0x05] = EVENT_MASTER_VOLUME;
  eventMap[statusByte + 0x06] = EVENT_MASTER_VOLUME_FADE;
  eventMap[statusByte + 0x07] = EVENT_TEMPO;
  eventMap[statusByte + 0x08] = EVENT_TEMPO_FADE;
  eventMap[statusByte + 0x09] = EVENT_GLOBAL_TRANSPOSE;
  eventMap[statusByte + 0x0a] = EVENT_TRANSPOSE;
  eventMap[statusByte + 0x0b] = EVENT_TREMOLO_ON;
  eventMap[statusByte + 0x0c] = EVENT_TREMOLO_OFF;
  eventMap[statusByte + 0x0d] = EVENT_VOLUME;
  eventMap[statusByte + 0x0e] = EVENT_VOLUME_FADE;
  eventMap[statusByte + 0x0f] = EVENT_CALL;
  eventMap[statusByte + 0x10] = EVENT_VIBRATO_FADE;
  eventMap[statusByte + 0x11] = EVENT_PITCH_ENVELOPE_TO;
  eventMap[statusByte + 0x12] = EVENT_PITCH_ENVELOPE_FROM;
  eventMap[statusByte + 0x13] = EVENT_PITCH_ENVELOPE_OFF;
  eventMap[statusByte + 0x14] = EVENT_TUNING;
  eventMap[statusByte + 0x15] = EVENT_ECHO_ON;
  eventMap[statusByte + 0x16] = EVENT_ECHO_OFF;
  eventMap[statusByte + 0x17] = EVENT_ECHO_PARAM;
  eventMap[statusByte + 0x18] = EVENT_ECHO_VOLUME_FADE;
  eventMap[statusByte + 0x19] = EVENT_PITCH_SLIDE;
  eventMap[statusByte + 0x1a] = EVENT_PERCUSSION_PATCH_BASE;
}

constexpr std::array<uint8_t, 16> kVolumeTableEarlier = {
    0x08, 0x12, 0x1b, 0x24, 0x2c, 0x35, 0x3e, 0x47,
    0x51, 0x5a, 0x62, 0x6b, 0x7d, 0x8f, 0xa1, 0xb3,
};

constexpr std::array<uint8_t, 8> kDurTableEarlier = {
    0x33, 0x66, 0x80, 0x99, 0xb3, 0xcc, 0xe6, 0xff,
};

constexpr std::array<uint8_t, 21> kPanTableEarlier = {
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
    0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
    0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
};

constexpr std::array<uint8_t, 16> kVolumeTableStandard = {
    0x19, 0x33, 0x4c, 0x66, 0x72, 0x7f, 0x8c, 0x99,
    0xa5, 0xb2, 0xbf, 0xcc, 0xd8, 0xe5, 0xf2, 0xfc,
};

constexpr std::array<uint8_t, 8> kDurTableStandard = {
    0x33, 0x66, 0x7f, 0x99, 0xb2, 0xcc, 0xe5, 0xfc,
};

constexpr std::array<uint8_t, 21> kPanTableStandard = {
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
    0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
    0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
};

constexpr std::array<uint8_t, 16> kVolumeTableIntelli = {
    0x19, 0x32, 0x4c, 0x65, 0x72, 0x7f, 0x8c, 0x98,
    0xa5, 0xb2, 0xbf, 0xcb, 0xd8, 0xe5, 0xf2, 0xfc,
};

constexpr std::array<uint8_t, 8> kDurTableIntelli = {
    0x32, 0x65, 0x7f, 0x98, 0xb2, 0xcb, 0xe5, 0xfc,
};

constexpr std::array<uint8_t, 64> kDurVolTableIntelliFe3 = {
    0x00, 0x0c, 0x19, 0x26, 0x33, 0x3f, 0x4c, 0x59,
    0x66, 0x72, 0x75, 0x77, 0x70, 0x7c, 0x7f, 0x82,
    0x84, 0x87, 0x89, 0x8c, 0x8e, 0x91, 0x93, 0x96,
    0x99, 0x9b, 0x9e, 0xa0, 0xa3, 0xa5, 0xa8, 0xaa,
    0xad, 0xaf, 0xb2, 0xb5, 0xb7, 0xba, 0xbc, 0xbf,
    0xc1, 0xc4, 0xc6, 0xc9, 0xcc, 0xce, 0xd1, 0xd3,
    0xd6, 0xd8, 0xdb, 0xdd, 0xe0, 0xe2, 0xe5, 0xe8,
    0xea, 0xed, 0xef, 0xf2, 0xf4, 0xf7, 0xf9, 0xfc,
};

constexpr std::array<uint8_t, 64> kDurVolTableIntelliFe4 = {
    0x19, 0x26, 0x33, 0x3f, 0x4c, 0x59, 0x66, 0x6d,
    0x70, 0x72, 0x75, 0x77, 0x70, 0x7c, 0x7f, 0x82,
    0x84, 0x87, 0x89, 0x8c, 0x8e, 0x91, 0x93, 0x96,
    0x99, 0x9b, 0x9e, 0xa0, 0xa3, 0xa5, 0xa8, 0xaa,
    0xad, 0xaf, 0xb2, 0xb5, 0xb7, 0xba, 0xbc, 0xbf,
    0xc1, 0xc4, 0xc6, 0xc9, 0xcc, 0xce, 0xd1, 0xd3,
    0xd6, 0xd8, 0xdb, 0xdd, 0xe0, 0xe2, 0xe5, 0xe8,
    0xea, 0xed, 0xef, 0xf2, 0xf4, 0xf7, 0xf9, 0xfc,
};

constexpr NinSnesProfile kUnknownProfile {
    NinSnesProfileId::Unknown,
    NINSNES_UNKNOWN,
    "Unknown",
    NinSnesBaseProfileId::Unknown,
    NinSnesAddressModelId::Direct,
    NinSnesPlaylistModelId::Unknown,
    NinSnesNoteParamModelId::Standard,
    NinSnesProgramResolverId::Direct,
    NinSnesPanModelId::StandardTable,
    NinSnesInstrumentLayoutId::Standard6Byte,
    NinSnesIntelliModeId::None,
};

constexpr std::array<NinSnesProfile, 17> kProfiles {{
    {NinSnesProfileId::Earlier,
     NINSNES_EARLIER,
     "Earlier",
     NinSnesBaseProfileId::Earlier,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::StandardPercussion,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Earlier5Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::Standard,
     NINSNES_STANDARD,
     "Standard",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::StandardPercussion,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::Rd1,
     NINSNES_RD1,
     "RD1",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::StandardPercussion,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::Rd2,
     NINSNES_RD2,
     "RD2",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::StandardPercussion,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::Hal,
     NINSNES_HAL,
     "HAL",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::StandardPercussion,
     NinSnesPanModelId::HalTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::Konami,
     NINSNES_KONAMI,
     "Konami",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::KonamiBase,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::StandardPercussion,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::KonamiTuningTable,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::Lemmings,
     NINSNES_LEMMINGS,
     "Lemmings",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Lemmings,
     NinSnesProgramResolverId::StandardPercussion,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::IntelliFe3,
     NINSNES_INTELLI_FE3,
     "Intelligent Systems FE3",
     NinSnesBaseProfileId::Intelli,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::IntelliTable,
     NinSnesProgramResolverId::Direct,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::Fe3},
    {NinSnesProfileId::IntelliTa,
     NINSNES_INTELLI_TA,
     "Intelligent Systems TA",
     NinSnesBaseProfileId::Intelli,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::IntelliTaOverride,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::Ta},
    {NinSnesProfileId::IntelliFe4,
     NINSNES_INTELLI_FE4,
     "Intelligent Systems FE4",
     NinSnesBaseProfileId::Intelli,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::IntelliTable,
     NinSnesProgramResolverId::Direct,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::Fe4},
    {NinSnesProfileId::Human,
     NINSNES_HUMAN,
     "Human",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::Direct,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::Tose,
     NINSNES_TOSE,
     "TOSE",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Tose,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::StandardPercussion,
     NinSnesPanModelId::ToseLinear,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::QuintetActR,
     NINSNES_QUINTET_ACTR,
     "Quintet ActRaiser",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::QuintetActRBase,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::QuintetActR2,
     NINSNES_QUINTET_ACTR2,
     "Quintet ActRaiser 2",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::QuintetLookup,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::QuintetIog,
     NINSNES_QUINTET_IOG,
     "Quintet Illusion of Gaia",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::QuintetLookup,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::QuintetTs,
     NINSNES_QUINTET_TS,
     "Quintet Terranigma",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::Direct,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::QuintetLookup,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
    {NinSnesProfileId::FalcomYs4,
     NINSNES_FALCOM_YS4,
     "Falcom Ys IV",
     NinSnesBaseProfileId::Standard,
     NinSnesAddressModelId::FalcomBaseOffset,
     NinSnesPlaylistModelId::Standard,
     NinSnesNoteParamModelId::Standard,
     NinSnesProgramResolverId::StandardPercussion,
     NinSnesPanModelId::StandardTable,
     NinSnesInstrumentLayoutId::Standard6Byte,
     NinSnesIntelliModeId::None},
}};

}  // namespace

const NinSnesProfile& getNinSnesProfile(NinSnesVersion version) {
  for (const auto& profile : kProfiles) {
    if (profile.legacyVersion == version) {
      return profile;
    }
  }

  return kUnknownProfile;
}

const NinSnesProfile& getNinSnesProfile(NinSnesProfileId id) {
  for (const auto& profile : kProfiles) {
    if (profile.id == id) {
      return profile;
    }
  }

  return kUnknownProfile;
}

NinSnesProfileId getNinSnesProfileId(NinSnesVersion version) {
  return getNinSnesProfile(version).id;
}

uint16_t convertNinSnesAddress(const NinSnesProfile& profile,
                               uint16_t rawAddress,
                               uint16_t konamiBaseAddress,
                               uint16_t falcomBaseOffset) {
  switch (profile.addressModel) {
    case NinSnesAddressModelId::KonamiBase:
      return static_cast<uint16_t>(konamiBaseAddress + rawAddress);

    case NinSnesAddressModelId::FalcomBaseOffset:
      return static_cast<uint16_t>(falcomBaseOffset + rawAddress);

    case NinSnesAddressModelId::Direct:
    default:
      return rawAddress;
  }
}

uint16_t readNinSnesAddress(const NinSnesProfile& profile,
                            const RawFile* file,
                            uint32_t offset,
                            uint16_t konamiBaseAddress,
                            uint16_t falcomBaseOffset) {
  return convertNinSnesAddress(profile, file->readShort(offset), konamiBaseAddress, falcomBaseOffset);
}

uint32_t getNinSnesInstrumentHeaderSize(const NinSnesProfile& profile) {
  return profile.instrumentLayout == NinSnesInstrumentLayoutId::Earlier5Byte ? 5 : 6;
}

uint16_t getNinSnesInstrumentSlotCount(const NinSnesProfile& profile) {
  if (profile.legacyVersion == NINSNES_HUMAN) {
    return static_cast<uint16_t>((0x200 / getNinSnesInstrumentHeaderSize(profile)) + 1);
  }

  return 0x80;
}

bool isBlankNinSnesInstrumentSlot(const NinSnesProfile& profile,
                                  const RawFile* file,
                                  uint32_t addrInstrHeader) {
  const uint32_t instrItemSize = getNinSnesInstrumentHeaderSize(profile);
  if (addrInstrHeader + instrItemSize > 0x10000) {
    return false;
  }

  if (profile.instrumentLayout == NinSnesInstrumentLayoutId::Earlier5Byte) {
    return false;
  }

  bool allZero = true;
  bool allFF = true;
  for (uint32_t i = 0; i < instrItemSize; i++) {
    const uint8_t b = file->readByte(addrInstrHeader + i);
    allZero &= (b == 0x00);
    allFF &= (b == 0xFF);
  }
  return allZero || allFF;
}

bool isValidNinSnesInstrumentHeader(const NinSnesProfile& profile,
                                    const RawFile* file,
                                    uint32_t addrInstrHeader,
                                    uint32_t spcDirAddr,
                                    bool validateSample) {
  const uint32_t instrItemSize = getNinSnesInstrumentHeaderSize(profile);
  if (addrInstrHeader + instrItemSize > 0x10000) {
    return false;
  }

  std::vector<uint8_t> instrHeader(instrItemSize);
  file->readBytes(addrInstrHeader, instrItemSize, instrHeader.data());
  if (std::all_of(instrHeader.cbegin(), instrHeader.cend(), [](uint8_t b) { return b == 0x00 || b == 0xFF; })) {
    return false;
  }

  const uint8_t srcn = instrHeader[0];
  const uint8_t adsr1 = instrHeader[1];
  const uint8_t gain = instrHeader[3];
  if (srcn >= 0x80 || (adsr1 == 0 && gain == 0)) {
    return false;
  }

  const uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
  if (!SNESSampColl::isValidSampleDir(file, addrDIRentry, validateSample)) {
    return false;
  }

  const uint16_t srcAddr = file->readShort(addrDIRentry);
  const uint16_t loopStartAddr = file->readShort(addrDIRentry + 2);
  if (srcAddr > loopStartAddr || (loopStartAddr - srcAddr) % 9 != 0) {
    return false;
  }

  return true;
}

bool requiresNinSnesSampleStartAfterDirEntry(const NinSnesProfile& profile) {
  return profile.legacyVersion == NINSNES_EARLIER || profile.legacyVersion == NINSNES_STANDARD;
}

bool loadsFullNinSnesSampleDirectory(const NinSnesProfile& profile) {
  return profile.legacyVersion == NINSNES_INTELLI_TA;
}

uint32_t resolveNinSnesProgramNumber(const NinSnesProfile& profile,
                                     const RawFile* file,
                                     uint8_t instrumentByte,
                                     uint8_t percussionStatusMin,
                                     uint8_t percussionBase,
                                     uint8_t quintetBgmInstrBase,
                                     uint16_t quintetInstrLookupAddr,
                                     const std::array<uint32_t, 0x80>* intelliInstrumentProgramMap,
                                     uint8_t* logicalProgram) {
  uint8_t resolvedLogicalProgram = instrumentByte;

  if (profile.programResolver != NinSnesProgramResolverId::Direct && instrumentByte >= 0x80) {
    resolvedLogicalProgram =
        static_cast<uint8_t>((instrumentByte - percussionStatusMin) + percussionBase);
  }

  switch (profile.programResolver) {
    case NinSnesProgramResolverId::QuintetActRBase:
      resolvedLogicalProgram = static_cast<uint8_t>(resolvedLogicalProgram + quintetBgmInstrBase);
      break;

    case NinSnesProgramResolverId::QuintetLookup:
      resolvedLogicalProgram = file->readByte(quintetInstrLookupAddr + resolvedLogicalProgram);
      break;

    case NinSnesProgramResolverId::IntelliTaOverride:
      break;

    case NinSnesProgramResolverId::StandardPercussion:
    case NinSnesProgramResolverId::Direct:
    default:
      break;
  }

  if (logicalProgram != nullptr) {
    *logicalProgram = resolvedLogicalProgram;
  }

  if (profile.programResolver == NinSnesProgramResolverId::IntelliTaOverride &&
      intelliInstrumentProgramMap != nullptr &&
      resolvedLogicalProgram < intelliInstrumentProgramMap->size()) {
    return (*intelliInstrumentProgramMap)[resolvedLogicalProgram];
  }

  return resolvedLogicalProgram;
}

bool usesNinSnesIntelliCustomPercTable(const NinSnesProfile& profile,
                                       bool runtimeCustomPercTable,
                                       uint8_t intelliPercFlags) {
  if (profile.legacyVersion == NINSNES_INTELLI_FE4) {
    return true;
  }

  if (profile.legacyVersion == NINSNES_INTELLI_TA) {
    return (intelliPercFlags & 0x40) != 0;
  }

  return runtimeCustomPercTable;
}

void setNinSnesIntelliCustomPercTableEnabled(const NinSnesProfile& profile,
                                             bool enabled,
                                             bool& runtimeCustomPercTable,
                                             uint8_t& intelliPercFlags) {
  if (profile.legacyVersion == NINSNES_INTELLI_TA || profile.legacyVersion == NINSNES_INTELLI_FE4) {
    if (enabled) {
      intelliPercFlags |= 0x40;
    }
    else {
      intelliPercFlags &= static_cast<uint8_t>(~0x40);
    }
    return;
  }

  runtimeCustomPercTable = enabled;
}

uint8_t readNinSnesPanTable(const NinSnesProfile& profile,
                            const std::vector<uint8_t>& panTable,
                            uint16_t pan) {
  if (profile.panModel == NinSnesPanModelId::ToseLinear) {
    return 0;
  }

  if (panTable.empty()) {
    return 0;
  }

  uint8_t panIndex = pan >> 8;
  uint8_t panFraction = pan & 0xff;

  uint8_t panMaxIndex = static_cast<uint8_t>(panTable.size() - 1);
  if (panIndex > panMaxIndex) {
    panIndex = panMaxIndex;
    panFraction = 0;
  }

  uint8_t volumeRate = panTable[panIndex];
  uint8_t nextVolumeRate = (panIndex < panMaxIndex) ? panTable[panIndex + 1] : volumeRate;
  uint8_t volumeRateDelta = nextVolumeRate - volumeRate;
  volumeRate += (volumeRateDelta * panFraction) >> 8;
  return volumeRate;
}

void getNinSnesVolumeBalance(const NinSnesProfile& profile,
                             const std::vector<uint8_t>& panTable,
                             uint16_t pan,
                             double& volumeLeft,
                             double& volumeRight) {
  uint8_t panIndex = pan >> 8;
  if (profile.panModel == NinSnesPanModelId::ToseLinear) {
    if (panIndex <= 10) {
      volumeLeft = (255 - 25 * std::max(10 - panIndex, 0)) / 256.0;
      volumeRight = 1.0;
    }
    else {
      volumeLeft = 1.0;
      volumeRight = (255 - 25 * std::max(panIndex - 10, 0)) / 256.0;
    }
    return;
  }

  if (panTable.empty()) {
    volumeLeft = 1.0;
    volumeRight = 1.0;
    return;
  }

  uint8_t panMaxIndex = static_cast<uint8_t>(panTable.size() - 1);
  if (panIndex > panMaxIndex) {
    pan = panMaxIndex << 8;
    panIndex = panMaxIndex;
  }

  volumeRight = readNinSnesPanTable(profile, panTable, (panMaxIndex << 8) - pan) / 128.0;
  volumeLeft = readNinSnesPanTable(profile, panTable, pan) / 128.0;

  if (profile.panModel == NinSnesPanModelId::HalTable) {
    std::swap(volumeLeft, volumeRight);
  }
}

NinSnesPanState decodeNinSnesPanValue(const NinSnesProfile& profile, uint8_t pan) {
  if (profile.panModel == NinSnesPanModelId::ToseLinear) {
    return {pan, false, false};
  }

  return {
      static_cast<uint8_t>(pan & 0x1f),
      (pan & 0x80) != 0,
      (pan & 0x40) != 0,
  };
}

NinSnesSeqDefinition buildNinSnesSeqDefinition(NinSnesVersion version,
                                               const std::vector<uint8_t>& volumeTable,
                                               const std::vector<uint8_t>& durRateTable,
                                               const std::vector<uint8_t>& panTable,
                                               const std::vector<uint8_t>& intelliDurVolTable) {
  NinSnesSeqDefinition definition;
  definition.volumeTable = volumeTable;
  definition.durRateTable = durRateTable;
  definition.panTable = panTable;
  definition.intelliDurVolTable = intelliDurVolTable;

  if (version == NINSNES_UNKNOWN) {
    return definition;
  }

  switch (version) {
    case NINSNES_EARLIER:
      definition.status = {0x00, 0x80, 0xc5, 0xd0, 0xd9};
      break;

    default:
      definition.status = {0x00, 0x80, 0xc7, 0xca, 0xdf};
      break;
  }

  definition.eventMap[0x00] = EVENT_END;

  for (int statusByte = 0x01; statusByte < definition.status.noteMin; statusByte++) {
    definition.eventMap[static_cast<uint8_t>(statusByte)] = EVENT_NOTE_PARAM;
  }

  for (int statusByte = definition.status.noteMin; statusByte <= definition.status.noteMax; statusByte++) {
    definition.eventMap[static_cast<uint8_t>(statusByte)] = EVENT_NOTE;
  }

  definition.eventMap[definition.status.noteMax + 1] = EVENT_TIE;
  definition.eventMap[definition.status.noteMax + 2] = EVENT_REST;

  for (int statusByte = definition.status.percussionNoteMin;
       statusByte <= definition.status.percussionNoteMax;
       statusByte++) {
    definition.eventMap[static_cast<uint8_t>(statusByte)] = EVENT_PERCUSSION_NOTE;
  }

  switch (version) {
    case NINSNES_EARLIER:
      definition.eventMap[0xda] = EVENT_PROGCHANGE;
      definition.eventMap[0xdb] = EVENT_PAN;
      definition.eventMap[0xdc] = EVENT_PAN_FADE;
      definition.eventMap[0xdd] = EVENT_PITCH_SLIDE;
      definition.eventMap[0xde] = EVENT_VIBRATO_ON;
      definition.eventMap[0xdf] = EVENT_VIBRATO_OFF;
      definition.eventMap[0xe0] = EVENT_MASTER_VOLUME;
      definition.eventMap[0xe1] = EVENT_MASTER_VOLUME_FADE;
      definition.eventMap[0xe2] = EVENT_TEMPO;
      definition.eventMap[0xe3] = EVENT_TEMPO_FADE;
      definition.eventMap[0xe4] = EVENT_GLOBAL_TRANSPOSE;
      definition.eventMap[0xe5] = EVENT_TREMOLO_ON;
      definition.eventMap[0xe6] = EVENT_TREMOLO_OFF;
      definition.eventMap[0xe7] = EVENT_VOLUME;
      definition.eventMap[0xe8] = EVENT_VOLUME_FADE;
      definition.eventMap[0xe9] = EVENT_CALL;
      definition.eventMap[0xea] = EVENT_VIBRATO_FADE;
      definition.eventMap[0xeb] = EVENT_PITCH_ENVELOPE_TO;
      definition.eventMap[0xec] = EVENT_PITCH_ENVELOPE_FROM;
      definition.eventMap[0xee] = EVENT_TUNING;
      definition.eventMap[0xef] = EVENT_ECHO_ON;
      definition.eventMap[0xf0] = EVENT_ECHO_OFF;
      definition.eventMap[0xf1] = EVENT_ECHO_PARAM;
      definition.eventMap[0xf2] = EVENT_ECHO_VOLUME_FADE;

      if (definition.volumeTable.empty()) {
        definition.volumeTable = defaultTable(kVolumeTableEarlier);
      }
      if (definition.durRateTable.empty()) {
        definition.durRateTable = defaultTable(kDurTableEarlier);
      }
      if (definition.panTable.empty()) {
        definition.panTable = defaultTable(kPanTableEarlier);
      }
      break;

    case NINSNES_INTELLI_FE3:
      for (int statusByte = 0x01; statusByte < definition.status.noteMin; statusByte++) {
        definition.eventMap[static_cast<uint8_t>(statusByte)] = EVENT_INTELLI_NOTE_PARAM;
      }
      loadStandardVcmdMap(definition.eventMap, 0xd6);
      definition.eventMap[0xf1] = EVENT_INTELLI_ECHO_ON;
      definition.eventMap[0xf2] = EVENT_INTELLI_ECHO_OFF;
      definition.eventMap[0xf3] = EVENT_INTELLI_LEGATO_ON;
      definition.eventMap[0xf4] = EVENT_INTELLI_LEGATO_OFF;
      definition.eventMap[0xf5] = EVENT_INTELLI_FE3_EVENT_F5;
      definition.eventMap[0xf6] = EVENT_INTELLI_WRITE_APU_PORT;
      definition.eventMap[0xf7] = EVENT_INTELLI_JUMP_SHORT_CONDITIONAL;
      definition.eventMap[0xf8] = EVENT_INTELLI_JUMP_SHORT;
      definition.eventMap[0xf9] = EVENT_INTELLI_FE3_EVENT_F9;
      definition.eventMap[0xfa] = EVENT_INTELLI_DEFINE_VOICE_PARAM;
      definition.eventMap[0xfb] = EVENT_INTELLI_LOAD_VOICE_PARAM;
      definition.eventMap[0xfc] = EVENT_INTELLI_ADSR;
      definition.eventMap[0xfd] = EVENT_INTELLI_GAIN_SUSTAIN_TIME_AND_RATE;

      if (definition.volumeTable.empty()) {
        definition.volumeTable = defaultTable(kVolumeTableIntelli);
      }
      if (definition.durRateTable.empty()) {
        definition.durRateTable = defaultTable(kDurTableIntelli);
      }
      if (definition.intelliDurVolTable.empty()) {
        definition.intelliDurVolTable = defaultTable(kDurVolTableIntelliFe3);
      }
      if (definition.panTable.empty()) {
        definition.panTable = defaultTable(kPanTableStandard);
      }
      break;

    case NINSNES_INTELLI_TA:
      loadStandardVcmdMap(definition.eventMap, 0xda);
      definition.eventMap[0xf5] = EVENT_INTELLI_ECHO_ON;
      definition.eventMap[0xf6] = EVENT_INTELLI_ECHO_OFF;
      definition.eventMap[0xf7] = EVENT_INTELLI_ADSR;
      definition.eventMap[0xf8] = EVENT_INTELLI_GAIN_SUSTAIN_TIME_AND_RATE;
      definition.eventMap[0xf9] = EVENT_INTELLI_GAIN_SUSTAIN_TIME;
      definition.eventMap[0xfa] = EVENT_INTELLI_DEFINE_VOICE_PARAM;
      definition.eventMap[0xfb] = EVENT_INTELLI_LOAD_VOICE_PARAM;
      definition.eventMap[0xfc] = EVENT_INTELLI_FE4_EVENT_FC;
      definition.eventMap[0xfd] = EVENT_INTELLI_TA_SUBEVENT;

      if (definition.volumeTable.empty()) {
        definition.volumeTable = defaultTable(kVolumeTableIntelli);
      }
      if (definition.durRateTable.empty()) {
        definition.durRateTable = defaultTable(kDurTableIntelli);
      }
      if (definition.panTable.empty()) {
        definition.panTable = defaultTable(kPanTableStandard);
      }
      break;

    case NINSNES_INTELLI_FE4:
      for (int statusByte = 0x01; statusByte < definition.status.noteMin; statusByte++) {
        definition.eventMap[static_cast<uint8_t>(statusByte)] = EVENT_INTELLI_NOTE_PARAM;
      }
      loadStandardVcmdMap(definition.eventMap, 0xda);
      definition.eventMap[0xf5] = EVENT_INTELLI_ECHO_ON;
      definition.eventMap[0xf6] = EVENT_INTELLI_ECHO_OFF;
      definition.eventMap[0xf7] = EVENT_INTELLI_GAIN;
      definition.eventMap[0xf8] = EVENT_INTELLI_GAIN;
      definition.eventMap[0xf9] = EVENT_UNKNOWN0;
      definition.eventMap[0xfa] = EVENT_INTELLI_DEFINE_VOICE_PARAM;
      definition.eventMap[0xfb] = EVENT_INTELLI_LOAD_VOICE_PARAM;
      definition.eventMap[0xfc] = EVENT_INTELLI_FE4_EVENT_FC;
      definition.eventMap[0xfd] = EVENT_INTELLI_FE4_SUBEVENT;

      if (definition.intelliDurVolTable.empty()) {
        definition.intelliDurVolTable = defaultTable(kDurVolTableIntelliFe4);
      }
      if (definition.panTable.empty()) {
        definition.panTable = defaultTable(kPanTableStandard);
      }
      break;

    default:
      loadStandardVcmdMap(definition.eventMap, 0xe0);

      if (definition.volumeTable.empty()) {
        definition.volumeTable = defaultTable(kVolumeTableStandard);
      }
      if (definition.durRateTable.empty()) {
        definition.durRateTable = defaultTable(kDurTableStandard);
      }
      if (definition.panTable.empty()) {
        definition.panTable = defaultTable(kPanTableStandard);
      }
      break;
  }

  switch (version) {
    case NINSNES_RD1:
      definition.eventMap[0xfb] = EVENT_UNKNOWN2;
      definition.eventMap[0xfc] = EVENT_UNKNOWN0;
      definition.eventMap[0xfd] = EVENT_UNKNOWN0;
      definition.eventMap[0xfe] = EVENT_UNKNOWN0;
      break;

    case NINSNES_RD2:
      definition.eventMap[0xfb] = EVENT_RD2_PROGCHANGE_AND_ADSR;
      definition.eventMap[0xfd] = EVENT_PROGCHANGE;
      break;

    case NINSNES_KONAMI:
      definition.eventMap[0xe4] = EVENT_UNKNOWN2;
      definition.eventMap[0xe5] = EVENT_KONAMI_LOOP_START;
      definition.eventMap[0xe6] = EVENT_KONAMI_LOOP_END;
      definition.eventMap[0xe8] = EVENT_NOP;
      definition.eventMap[0xe9] = EVENT_NOP;
      definition.eventMap[0xf5] = EVENT_UNKNOWN0;
      definition.eventMap[0xf6] = EVENT_UNKNOWN0;
      definition.eventMap[0xf7] = EVENT_UNKNOWN0;
      definition.eventMap[0xf8] = EVENT_UNKNOWN0;
      definition.eventMap[0xfb] = EVENT_KONAMI_ADSR_AND_GAIN;
      definition.eventMap[0xfc] = EVENT_NOP;
      definition.eventMap[0xfd] = EVENT_NOP;
      definition.eventMap[0xfe] = EVENT_NOP;
      break;

    case NINSNES_LEMMINGS:
      for (int statusByte = 0x01; statusByte < definition.status.noteMin; statusByte++) {
        definition.eventMap[static_cast<uint8_t>(statusByte)] = EVENT_LEMMINGS_NOTE_PARAM;
      }

      definition.eventMap[0xe5] = EVENT_UNKNOWN1;
      definition.eventMap[0xe6] = EVENT_UNKNOWN2;
      definition.eventMap[0xfb] = EVENT_NOP1;
      definition.eventMap[0xfc] = EVENT_UNKNOWN0;
      definition.eventMap[0xfd] = EVENT_UNKNOWN0;
      definition.eventMap[0xfe] = EVENT_UNKNOWN0;
      definition.volumeTable.clear();
      definition.durRateTable.clear();
      break;

    case NINSNES_TOSE:
      definition.panTable.clear();
      break;

    case NINSNES_QUINTET_IOG:
    case NINSNES_QUINTET_TS:
      definition.eventMap[0xf4] = EVENT_QUINTET_TUNING;
      definition.eventMap[0xff] = EVENT_QUINTET_ADSR;
      break;

    case NINSNES_HAL:
    case NINSNES_HUMAN:
    default:
      break;
  }

  return definition;
}
