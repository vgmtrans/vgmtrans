#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "TamSoftPS1Format.h"

class TamSoftPS1Seq:
    public VGMSeq {
 public:
  TamSoftPS1Seq(RawFile *file, uint32_t offset, uint8_t theSong, const std::string &name = "TamSoftPS1Seq");
  virtual ~TamSoftPS1Seq();

  virtual bool parseHeader();
  virtual bool parseTrackPointers();
  virtual void resetVars();

 public:
  static const uint16_t PITCH_TABLE[73];

  uint8_t song;
  uint16_t type;
  int16_t reverbDepth;
  bool ps2;
};


class TamSoftPS1Track
    : public SeqTrack {
 public:
  TamSoftPS1Track(TamSoftPS1Seq *parentSeq, uint32_t offset);

  virtual void resetVars();
  virtual State readEvent();

 protected:
  void finalizeAllNotes();

  uint32_t lastNoteTime;
  uint16_t lastNotePitch;
  int8_t lastNoteKey;
};
