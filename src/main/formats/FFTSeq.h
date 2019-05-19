/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "Format.h"  //Replace with MP2k-specific format header when that's ready

static const unsigned char delta_time_table[] = {0,  192, 144, 96, 72, 64, 48, 36, 32, 24,
                                                 18, 16,  12,  9,  8,  6,  4,  3,  2};

class FFTSeq : public VGMSeq {
   public:
    FFTSeq(RawFile *file, uint32_t offset);
    virtual ~FFTSeq(void);

    virtual bool GetHeaderInfo(void);
    virtual bool GetTrackPointers(
        void);  // Function to find all of the track pointers.   Returns number of total tracks.
    virtual uint32_t GetID() { return assocWdsID; }

   protected:
    uint16_t seqID;
    uint16_t assocWdsID;
};

class FFTTrack : public SeqTrack {
   public:
    FFTTrack(FFTSeq *parentFile, long offset = 0, long length = 0);
    virtual void ResetVars();
    virtual bool ReadEvent(void);

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
    uint8_t loop_octave[5];      // 1,Sep.2009 revise
    uint8_t loop_end_octave[5];  // 1,Sep.2009 revise
};
