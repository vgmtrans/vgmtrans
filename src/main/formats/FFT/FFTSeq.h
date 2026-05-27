#pragma once

#include "base/Types.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

static const unsigned char delta_time_table[] = {0, 192, 144, 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2};

class FFTSeq : public VGMSeq {
 public:
  FFTSeq(RawFile *file, u32 offset);
  ~FFTSeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  u32 id() const override { return assocWdsID; }

 protected:
  u16 seqID;
  u16 assocWdsID;
};


class FFTTrack
    : public SeqTrack {
 public:
  FFTTrack(FFTSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  bool readEvent() override;

 public:
  bool bNoteOn;
  u32 infiniteLoopPt;
  u8 infiniteLoopOctave;
  u32 loopID[5];
  int loop_counter[5];
  int loop_repeats[5];
  int loop_layer;
  u32 loop_begin_loc[5];
  u32 loop_end_loc[5];
  u8 loop_octave[5];        //1,Sep.2009 revise
  u8 loop_end_octave[5];    //1,Sep.2009 revise
};
