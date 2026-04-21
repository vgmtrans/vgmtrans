#pragma once

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

const NinSnesProfile& getNinSnesProfile(NinSnesVersion version);
NinSnesProfileId getNinSnesProfileId(NinSnesVersion version);
