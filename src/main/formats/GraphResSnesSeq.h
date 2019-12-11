/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "GraphResSnesFormat.h"

#define GRAPHRESSNES_CALLSTACK_SIZE 2
#define GRAPHRESSNES_LOOP_LEVEL_MAX 4

enum GraphResSnesSeqEventType {
    // start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get
    // confused with a legit event
    EVENT_UNKNOWN0 = 1,
    EVENT_UNKNOWN1,
    EVENT_UNKNOWN2,
    EVENT_UNKNOWN3,
    EVENT_UNKNOWN4,
    EVENT_NOTE,
    EVENT_INSTANT_VOLUME,
    EVENT_INSTANT_OCTAVE,
    EVENT_TRANSPOSE,
    EVENT_MASTER_VOLUME,
    EVENT_ECHO_VOLUME,
    EVENT_DEC_OCTAVE,
    EVENT_INC_OCTAVE,
    EVENT_LOOP_BREAK,
    EVENT_LOOP_START,
    EVENT_LOOP_END,
    EVENT_DURATION_RATE,
    EVENT_DSP_WRITE,
    EVENT_LOOP_AGAIN_NO_NEST,
    EVENT_NOISE_TOGGLE,
    EVENT_VOLUME,
    EVENT_MASTER_VOLUME_FADE,
    EVENT_PAN,
    EVENT_ADSR,
    EVENT_RET,
    EVENT_CALL,
    EVENT_GOTO,
    EVENT_PROGCHANGE,
    EVENT_DEFAULT_LENGTH,
    EVENT_SLUR,
    EVENT_END,
};

class GraphResSnesSeq : public VGMSeq {
   public:
    GraphResSnesSeq(RawFile *file, GraphResSnesVersion ver, uint32_t seqdata_offset,
                    std::string newName = "GraphRes SNES Seq");
    virtual ~GraphResSnesSeq(void);

    virtual bool GetHeaderInfo(void);
    virtual bool GetTrackPointers(void);
    virtual void ResetVars(void);

    GraphResSnesVersion version;
    std::map<uint8_t, GraphResSnesSeqEventType> EventMap;

    std::map<uint8_t, uint16_t> instrADSRHints;

   private:
    void LoadEventMap(void);
};

class GraphResSnesTrack : public SeqTrack {
   public:
    GraphResSnesTrack(GraphResSnesSeq *parentFile, long offset = 0, long length = 0);
    virtual void ResetVars(void);
    virtual bool ReadEvent(void);

   private:
    int8_t prevNoteKey;
    bool prevNoteSlurred;
    uint8_t durationRate;
    uint8_t defaultNoteLength;
    int8_t spcPan;
    uint8_t spcInstr;
    uint16_t spcADSR;
    uint16_t callStack[GRAPHRESSNES_CALLSTACK_SIZE];
    uint8_t callStackPtr;

    uint8_t loopStackPtr;
    int8_t loopCount[GRAPHRESSNES_LOOP_LEVEL_MAX];
    uint16_t loopEnd[GRAPHRESSNES_LOOP_LEVEL_MAX];
};
