#pragma once

#include <array>
#include <map>
#include <vector>

#include "NinSnesSeqState.h"
#include "NinSnesTypes.h"

class RawFile;

struct NinSnesProfile {
  NinSnesProfileId id;
  const char* name;
  NinSnesBaseProfileId baseProfile;
  NinSnesAddressModelId addressModel;
  NinSnesPlaylistModelId playlistModel;
  NinSnesNoteParamModelId noteParamModel;
  NinSnesProgramResolverId programResolver;
  NinSnesPanModelId panModel;
  NinSnesInstrumentLayoutId instrumentLayout;
  NinSnesInstrTableAddressModelId instrTableAddressModel;
  NinSnesIntelliModeId intelliMode;
};

struct NinSnesSeqStatus {
  uint8_t end = 0;
  uint8_t noteMin = 0;
  uint8_t noteMax = 0;
  uint8_t percussionNoteMin = 0;
  uint8_t percussionNoteMax = 0;
};

struct NinSnesSeqDefinition {
  NinSnesSeqStatus status;
  std::map<uint8_t, NinSnesSeqEventType> eventMap;
  std::vector<uint8_t> volumeTable;
  std::vector<uint8_t> durRateTable;
  std::vector<uint8_t> panTable;
  std::vector<uint8_t> intelliDurVolTable;
};

struct NinSnesPanState {
  uint8_t panIndex = 0;
  bool reverseLeft = false;
  bool reverseRight = false;
};

const NinSnesProfile& getNinSnesProfile(NinSnesProfileId id);
uint16_t convertNinSnesAddress(const NinSnesProfile& profile, uint16_t rawAddress,
                               uint16_t konamiBaseAddress, uint16_t falcomBaseOffset);
uint16_t readNinSnesAddress(const NinSnesProfile& profile, const RawFile* file, uint32_t offset,
                            uint16_t konamiBaseAddress, uint16_t falcomBaseOffset);
uint32_t getNinSnesInstrumentHeaderSize(const NinSnesProfile& profile);
uint16_t getNinSnesInstrumentSlotCount(const NinSnesProfile& profile);
bool isBlankNinSnesInstrumentSlot(const NinSnesProfile& profile, const RawFile* file,
                                  uint32_t addrInstrHeader);
bool isValidNinSnesInstrumentHeader(const NinSnesProfile& profile, const RawFile* file,
                                    uint32_t addrInstrHeader, uint32_t spcDirAddr,
                                    bool validateSample);
bool requiresNinSnesSampleStartAfterDirEntry(const NinSnesProfile& profile);
bool loadsFullNinSnesSampleDirectory(const NinSnesProfile& profile);
uint32_t resolveNinSnesProgramNumber(const NinSnesProfile& profile, const RawFile* file,
                                     uint8_t instrumentByte, uint8_t percussionStatusMin,
                                     uint8_t percussionBase, uint8_t quintetBgmInstrBase,
                                     uint16_t quintetInstrLookupAddr,
                                     const std::array<uint32_t, 0x80>* intelliInstrumentProgramMap,
                                     uint8_t* logicalProgram = nullptr);
bool usesNinSnesIntelliCustomPercTable(const NinSnesProfile& profile, bool runtimeCustomPercTable,
                                       uint8_t intelliPercFlags);
void setNinSnesIntelliCustomPercTableEnabled(const NinSnesProfile& profile, bool enabled,
                                             bool& runtimeCustomPercTable,
                                             uint8_t& intelliPercFlags);
uint8_t readNinSnesPanTable(const NinSnesProfile& profile, const std::vector<uint8_t>& panTable,
                            uint16_t pan);
void getNinSnesVolumeBalance(const NinSnesProfile& profile, const std::vector<uint8_t>& panTable,
                             uint16_t pan, double& volumeLeft, double& volumeRight);
NinSnesPanState decodeNinSnesPanValue(const NinSnesProfile& profile, uint8_t pan);

NinSnesSeqDefinition buildNinSnesSeqDefinition(NinSnesProfileId profileId,
                                               const std::vector<uint8_t>& volumeTable,
                                               const std::vector<uint8_t>& durRateTable,
                                               const std::vector<uint8_t>& panTable,
                                               const std::vector<uint8_t>& intelliDurVolTable);
