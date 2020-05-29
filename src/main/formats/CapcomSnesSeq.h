/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "CapcomSnesFormat.h"

#define CAPCOM_SNES_REPEAT_SLOT_MAX 4

#define CAPCOM_SNES_MASK_NOTE_OCTAVE 0x07
#define CAPCOM_SNES_MASK_NOTE_OCTAVE_UP 0x08
#define CAPCOM_SNES_MASK_NOTE_DOTTED 0x10
#define CAPCOM_SNES_MASK_NOTE_TRIPLET 0x20
#define CAPCOM_SNES_MASK_NOTE_SLURRED 0x40

enum CapcomSnesSeqEventType {
    // start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get
    // confused with a legit event
    EVENT_UNKNOWN0 = 1,
    EVENT_UNKNOWN1,
    EVENT_UNKNOWN2,
    EVENT_UNKNOWN3,
    EVENT_UNKNOWN4,
    EVENT_TOGGLE_TRIPLET,
    EVENT_TOGGLE_SLUR,
    EVENT_DOTTED_NOTE_ON,
    EVENT_TOGGLE_OCTAVE_UP,
    EVENT_NOTE_ATTRIBUTES,
    EVENT_TEMPO,
    EVENT_DURATION,
    EVENT_VOLUME,
    EVENT_PROGRAM_CHANGE,
    EVENT_OCTAVE,
    EVENT_GLOBAL_CISPOSE,
    EVENT_CISPOSE,
    EVENT_TUNING,
    EVENT_PORTAMENTO_TIME,
    EVENT_REPEAT_UNTIL_1,
    EVENT_REPEAT_UNTIL_2,
    EVENT_REPEAT_UNTIL_3,
    EVENT_REPEAT_UNTIL_4,
    EVENT_REPEAT_BREAK_1,
    EVENT_REPEAT_BREAK_2,
    EVENT_REPEAT_BREAK_3,
    EVENT_REPEAT_BREAK_4,
    EVENT_GOTO,
    EVENT_END,
    EVENT_PAN,
    EVENT_MASTER_VOLUME,
    EVENT_LFO,
    EVENT_ECHO_PARAM,
    EVENT_ECHO_ONOFF,
    EVENT_RELEASE_RATE,
    EVENT_NOP,
};

class CapcomSnesSeq : public VGMSeq {
   public:
    CapcomSnesSeq(RawFile *file, CapcomSnesVersion ver, uint32_t seqdata_offset,
                  bool priorityInHeader, std::string newName = "Capcom SNES Seq");
    virtual ~CapcomSnesSeq(void);

    virtual bool GetHeaderInfo(void);
    virtual bool GetTrackPointers(void);
    virtual void ResetVars(void);

    CapcomSnesVersion version;
    std::map<uint8_t, CapcomSnesSeqEventType> EventMap;

    bool priorityInHeader;
    int8_t cispose;
    uint16_t tempo;
    uint8_t midiReverb;

    static const uint8_t volTable[];
    static const uint8_t panTable[];

    double GetTempoInBPM();
    double GetTempoInBPM(uint16_t tempo);

   private:
    void LoadEventMap(void);
};

class CapcomSnesTrack : public SeqTrack {
   public:
    CapcomSnesTrack(CapcomSnesSeq *parentFile, long offset = 0, long length = 0);
    virtual void ResetVars(void);
    virtual bool ReadEvent(void);
    virtual void OnTickBegin(void);
    virtual void OnTickEnd(void);

    uint8_t getNoteOctave(void);
    void setNoteOctave(uint8_t octave);
    bool isNoteOctaveUp(void);
    void setNoteOctaveUp(bool octave_up);
    bool isNoteDotted(void);
    void setNoteDotted(bool dotted);
    bool isNoteTriplet(void);
    void setNoteTriplet(bool triplet);
    bool isNoteSlurred(void);
    void setNoteSlurred(bool slurred);

   private:
    uint8_t repeatCount[CAPCOM_SNES_REPEAT_SLOT_MAX];  // repeat count for repeat command
    uint8_t noteAttributes;
    uint8_t durationRate;
    // int8_t cispose;

    bool lastNoteSlurred;
    int8_t lastKey;

    double GetTuningInSemitones(int8_t tuning);
};
