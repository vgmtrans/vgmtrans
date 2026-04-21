#pragma once

#include <map>
#include <vector>

#include "NinSnesSeqState.h"
#include "NinSnesTypes.h"

struct NinSnesProfile {
  NinSnesProfileId id;
  NinSnesVersion legacyVersion;
  const char* name;
  NinSnesBaseProfileId baseProfile;
  NinSnesAddressModelId addressModel;
  NinSnesPlaylistModelId playlistModel;
  NinSnesNoteParamModelId noteParamModel;
  NinSnesProgramResolverId programResolver;
  NinSnesPanModelId panModel;
  NinSnesInstrumentLayoutId instrumentLayout;
  bool supportsIntelliVoiceParams;
  bool supportsDynamicInstrumentOverrides;
  bool supportsDynamicDrumKitExport;
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

const NinSnesProfile& getNinSnesProfile(NinSnesVersion version);
NinSnesProfileId getNinSnesProfileId(NinSnesVersion version);
NinSnesSeqDefinition buildNinSnesSeqDefinition(NinSnesVersion version,
                                               const std::vector<uint8_t>& volumeTable,
                                               const std::vector<uint8_t>& durRateTable,
                                               const std::vector<uint8_t>& panTable,
                                               const std::vector<uint8_t>& intelliDurVolTable);
