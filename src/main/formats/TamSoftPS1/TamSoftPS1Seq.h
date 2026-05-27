#pragma once

#include "base/Types.h"
#include "SeqEvent.h"
#include "SeqTrack.h"
#include "TamSoftPS1Format.h"
#include "VGMSeq.h"

#include <string>

class TamSoftPS1Seq:
    public VGMSeq {
 public:
  TamSoftPS1Seq(RawFile *file, u32 offset, u8 theSong, const std::string &name = "TamSoftPS1Seq");
  virtual ~TamSoftPS1Seq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  virtual void resetVars();

 public:
  static const u16 PITCH_TABLE[73];

  u8 song;
  u16 type;
  s16 reverbDepth;
  bool ps2;
};


class TamSoftPS1Track
    : public SeqTrack {
 public:
  TamSoftPS1Track(TamSoftPS1Seq *parentSeq, u32 offset);

  virtual void resetVars();
  virtual bool readEvent();

 protected:
  void finalizeAllNotes();

  u32 lastNoteTime;
  u16 lastNotePitch;
  s8 lastNoteKey;
};
