#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"

static const unsigned char delta_time_table[] = {0, 192, 144, 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2};

class FFTSeq : public VGMSeq {
 public:
  FFTSeq(RawFile *file, uint32_t offset);
  ~FFTSeq() override;

  bool GetHeaderInfo() override;
  bool GetTrackPointers() override;
  uint32_t GetID() override { return assocWdsID; }

 protected:
  uint16_t seqID;
  uint16_t assocWdsID;
};


class FFTTrack
    : public SeqTrack {
 public:
  FFTTrack(FFTSeq *parentFile, long offset = 0, long length = 0);
  void ResetVars() override;
  bool ReadEvent() override;

 public:
  bool bNoteOn;
  uint32_t infiniteLoopPt;
  uint8_t infiniteLoopOctave;
  uint32_t loopID[5];
  int loop_counter[5];
  int loop_repeats[5];
  int loop_layer;
  uint32_t loop_begin_loc[5];
  uint32_t loop_end_loc[5];
  uint8_t loop_octave[5];        //1,Sep.2009 revise
  uint8_t loop_end_octave[5];    //1,Sep.2009 revise
};
