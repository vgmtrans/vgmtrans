/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "VGMFile.h"
#include "RawFile.h"
#include "MidiFile.h"

class SeqTrack;
class SeqEvent;
class ISeqSlider;

enum ReadMode : uint8_t {
    READMODE_ADD_TO_UI,
    READMODE_CONVERT_TO_MIDI,
    READMODE_FIND_DELTA_LENGTH
};

class VGMSeq : public VGMFile {
   public:
    VGMSeq(const std::string &format, RawFile *file, uint32_t offset, uint32_t length = 0,
           std::string name = "VGM Sequence");
    virtual ~VGMSeq(void);

    virtual Icon GetIcon() { return ICON_SEQ; }

    bool LoadVGMFile() override;
    virtual bool Load();  // Function to load all the information about the sequence
    virtual bool LoadMain();
    virtual bool GetHeaderInfo(void);
    virtual bool GetTrackPointers(
        void);  // Function to find all of the track pointers.   Returns number of total tracks.
    virtual void ResetVars(void);
    virtual MidiFile *ConvertToMidi();
    virtual MidiTrack *GetFirstMidiTrack();
    void SetPPQN(uint16_t ppqn);
    uint16_t GetPPQN(void);
    // void SetTimeSignature(uint8_t numer, denom);
    void AddInstrumentRef(uint32_t progNum);

    void UseReverb() { bReverb = true; }
    void HasMonophonicTracks() { bMonophonicTracks = true; }
    void UseLinearAmplitudeScale() { bUseLinearAmplitudeScale = true; }
    void AlwaysWriteInitialVol(uint8_t theVol = 100) {
        bAlwaysWriteInitialVol = true;
        initialVol = theVol;
    }
    void AlwaysWriteInitialExpression(uint8_t level = 127) {
        bAlwaysWriteInitialExpression = true;
        initialExpression = level;
    }
    void AlwaysWriteInitialReverb(uint8_t level = 127) {
        bAlwaysWriteInitialReverb = true;
        initialReverb = level;
    }
    void AlwaysWriteInitialPitchBendRange(uint8_t semitones, uint8_t cents) {
        bAlwaysWriteInitialPitchBendRange = true;
        initialPitchBendRangeSemiTones = semitones;
        initialPitchBendRangeCents = cents;
    }
    void AlwaysWriteInitialTempo(double tempoBPM) {
        bAlwaysWriteInitialTempo = true;
        initialTempoBPM = tempoBPM;
    }

    bool OnSaveAsJSON();

    bool OnSaveAsMidi(void);
    virtual bool SaveAsMidi(const std::string &filepath);

    virtual bool HasActiveTracks();
    virtual void InactivateAllTracks();
    virtual int GetForeverLoops();

   protected:
    virtual bool LoadTracks(ReadMode readMode, long stopTime = 1000000);
    virtual void LoadTracksMain(long stopTime);
    virtual bool PostLoad();

   public:
    uint32_t nNumTracks;
    ReadMode readMode;
    MidiFile *midi;
    double tempoBPM;
    uint16_t ppqn;
    long time;  // absolute current time (ticks)

    // attributes
    bool bMonophonicTracks;  // Only 1 voice at a time on a track.  We can assume note offs always
                             // use last note on key. which is important when drivers allow things
                             // like global cisposition events mid note
    bool bUseLinearAmplitudeScale;  // This will cause all all velocity, volume, and expression
                                    // events to be automatically converted from a linear scale to
                                    // MIDI's logarithmic scale
    bool bAlwaysWriteInitialTempo;
    bool bAlwaysWriteInitialVol;
    bool bAlwaysWriteInitialExpression;
    bool bAlwaysWriteInitialReverb;
    bool bAlwaysWriteInitialPitchBendRange;
    bool bAlwaysWriteInitialMono;
    bool bAllowDiscontinuousTrackData;

    // True if each tracks in a sequence needs to be loaded simultaneously in tick by tick, as the
    // real music player does. Pros:
    //   - It allows to share some variables between two or more tracks.
    //   - It might be useful when you want to emulate envelopes like vibrato for every ticks.
    // Cons:
    //   - It prohibits that each tracks have different timings (delta time).
    //   - It is not very suitable for formats like SMF format 1 (multitrack, delta time first
    //   format),
    //     because a track must pause the parsing every time a parser encounters to delta time
    //     event. It can be used anyway, but it is useless and annoys you, in most cases.
    bool bLoadTickByTick;

    uint8_t initialVol;
    uint8_t initialExpression;
    uint8_t initialReverb;
    uint8_t initialPitchBendRangeSemiTones, initialPitchBendRangeCents;
    double initialTempoBPM;

    bool bReverb;
    float reverbTime;

    std::vector<SeqTrack *> aTracks;  // array of track pointers
    std::vector<uint32_t> aInstrumentsUsed;

    std::vector<ISeqSlider *> aSliders;
};

extern uint8_t mode;
