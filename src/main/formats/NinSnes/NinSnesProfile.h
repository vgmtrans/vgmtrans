#pragma once

#include "base/types.h"

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
  u8 end = 0;
  u8 noteMin = 0;
  u8 noteMax = 0;
  u8 percussionNoteMin = 0;
  u8 percussionNoteMax = 0;
};

struct NinSnesSeqDefinition {
  NinSnesSeqStatus status;
  std::map<u8, NinSnesSeqEventType> eventMap;
  std::vector<u8> volumeTable;
  std::vector<u8> durRateTable;
  std::vector<u8> panTable;
  std::vector<u8> intelliDurVolTable;
};

struct NinSnesPanState {
  u8 panIndex = 0;
  bool reverseLeft = false;
  bool reverseRight = false;
};

const NinSnesProfile& getNinSnesProfile(NinSnesProfileId id);
u16 convertNinSnesAddress(const NinSnesProfile& profile, u16 rawAddress,
                               u16 konamiBaseAddress, u16 falcomBaseOffset);
u16 readNinSnesAddress(const NinSnesProfile& profile, const RawFile* file, u32 offset,
                            u16 konamiBaseAddress, u16 falcomBaseOffset);
u32 getNinSnesInstrumentHeaderSize(const NinSnesProfile& profile);
u16 getNinSnesInstrumentSlotCount(const NinSnesProfile& profile);
bool isBlankNinSnesInstrumentSlot(const NinSnesProfile& profile, const RawFile* file,
                                  u32 addrInstrHeader);
bool isValidNinSnesInstrumentHeader(const NinSnesProfile& profile, const RawFile* file,
                                    u32 addrInstrHeader, u32 spcDirAddr,
                                    bool validateSample);
bool requiresNinSnesSampleStartAfterDirEntry(const NinSnesProfile& profile);
bool loadsFullNinSnesSampleDirectory(const NinSnesProfile& profile);
u32 resolveNinSnesProgramNumber(const NinSnesProfile& profile, const RawFile* file,
                                     u8 instrumentByte, u8 percussionStatusMin,
                                     u8 percussionBase, u8 quintetBgmInstrBase,
                                     u16 quintetInstrLookupAddr,
                                     const std::array<u32, 0x80>* intelliInstrumentProgramMap,
                                     u8* logicalProgram = nullptr);
bool usesNinSnesIntelliCustomPercTable(const NinSnesProfile& profile, bool runtimeCustomPercTable,
                                       u8 intelliPercFlags);
void setNinSnesIntelliCustomPercTableEnabled(const NinSnesProfile& profile, bool enabled,
                                             bool& runtimeCustomPercTable,
                                             u8& intelliPercFlags);
u8 readNinSnesPanTable(const NinSnesProfile& profile, const std::vector<u8>& panTable,
                            u16 pan);
void getNinSnesVolumeBalance(const NinSnesProfile& profile, const std::vector<u8>& panTable,
                             u16 pan, double& volumeLeft, double& volumeRight);
NinSnesPanState decodeNinSnesPanValue(const NinSnesProfile& profile, u8 pan);

NinSnesSeqDefinition buildNinSnesSeqDefinition(NinSnesProfileId profileId,
                                               const std::vector<u8>& volumeTable,
                                               const std::vector<u8>& durRateTable,
                                               const std::vector<u8>& panTable,
                                               const std::vector<u8>& intelliDurVolTable);
