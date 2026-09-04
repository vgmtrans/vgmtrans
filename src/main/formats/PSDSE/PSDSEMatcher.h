#pragma once
#include "Matcher.h"
#include "PSDSEInstrSet.h"
#include "PSDSEPS2InstrSet.h"
#include "PSDSEScanner.h"

// PSDSEMatcher hooks the format's file-close callbacks so that the scanner's
// cross-file association state (g_loadedInstrSets / g_loadedSampColls) is kept
// free of dangling pointers when a loaded file is removed.

class PSDSEMatcher : public Matcher {
public:
  explicit PSDSEMatcher(Format* format) : Matcher(format) {}
  ~PSDSEMatcher() override = default;

protected:
  bool onCloseInstrSet(VGMInstrSet* instrSet) override {
    if (auto* psdseInstrSet = dynamic_cast<PSDSEInstrSet*>(instrSet)) {
      PSDSEScanner::onInstrSetClose(psdseInstrSet);
      return true;
    }
    if (auto* ps2InstrSet = dynamic_cast<PSDSEPS2InstrSet*>(instrSet)) {
      PSDSEScanner::onInstrSetClose(ps2InstrSet);
      return true;
    }
    return false;
  }

  bool onCloseSampColl(VGMSampColl* sampColl) override {
    if (auto* psdseSampColl = dynamic_cast<PSDSESampColl*>(sampColl)) {
      PSDSEScanner::onSampCollClose(psdseSampColl);
      return true;
    }
    if (auto* ps2SampColl = dynamic_cast<PSDSEPS2SampColl*>(sampColl)) {
      PSDSEScanner::onSampCollClose(ps2SampColl);
      return true;
    }
    return false;
  }
};
