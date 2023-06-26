#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "CPSSeq.h"

enum CPSFormatVer: uint8_t;

class CPSTrackV1
    : public SeqTrack {
public:
  CPSTrackV1(CPSSeq *parentSeq, long offset = 0, long length = 0);
  virtual void ResetVars();
  virtual bool ReadEvent(void);

private:
  CPSFormatVer GetVersion() { return ((CPSSeq *) this->parentSeq)->fmt_version; }

  bool bPrevNoteTie;
  uint8_t prevTieNote;
  uint8_t origTieNote;
  uint8_t curDeltaTable;
  uint8_t noteState;
  uint8_t bank;
  uint8_t loop[4];
  uint32_t loopOffset[4];    //used for detecting infinite loops
};