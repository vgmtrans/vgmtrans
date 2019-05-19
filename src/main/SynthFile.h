/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "common.h"
#include "RiffFile.h"

struct Loop;
class VGMSamp;

class SynthInstr;
class SynthRgn;
class SynthArt;
class SynthConnectionBlock;
class SynthSampInfo;
class SynthWave;

typedef enum { no_transform, concave_transform } Transform;

class SynthFile {
   public:
    SynthFile(const std::string synth_name = "Instrument Set");
    ~SynthFile(void);

    SynthInstr *AddInstr(uint32_t bank, uint32_t instrNum);
    SynthInstr *AddInstr(uint32_t bank, uint32_t instrNum, std::string Name);
    void DeleteInstr(uint32_t bank, uint32_t instrNum);
    SynthWave *AddWave(uint16_t formatTag, uint16_t channels, int samplesPerSec, int aveBytesPerSec,
                       uint16_t blockAlign, uint16_t bitsPerSample, uint32_t waveDataSize,
                       uint8_t *waveData, std::string name = "Unnamed Wave");
    void SetName(std::string synth_name);

    // int WriteDLSToBuffer(std::vector<uint8_t> &buf);
    // bool SaveDLSFile(const wchar_t* filepath);

   public:
    std::vector<SynthInstr *> vInstrs;
    std::vector<SynthWave *> vWaves;
    std::string name;
};

class SynthInstr {
   public:
    SynthInstr(void);
    SynthInstr(uint32_t bank, uint32_t instrument);
    SynthInstr(uint32_t bank, uint32_t instrument, std::string instrName);
    SynthInstr(uint32_t bank, uint32_t instrument, std::string instrName,
               std::vector<SynthRgn *> listRgns);
    ~SynthInstr(void);

    void AddRgnList(std::vector<SynthRgn> &RgnList);
    SynthRgn *AddRgn(void);
    SynthRgn *AddRgn(SynthRgn rgn);

   public:
    uint32_t ulBank;
    uint32_t ulInstrument;

    std::vector<SynthRgn *> vRgns;
    std::string name;
};

class SynthRgn {
   public:
    SynthRgn(void) : sampinfo(NULL), art(NULL) {}
    SynthRgn(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh)
        : usKeyLow(keyLow),
          usKeyHigh(keyHigh),
          usVelLow(velLow),
          usVelHigh(velHigh),
          sampinfo(NULL),
          art(NULL) {}
    SynthRgn(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh, SynthArt &art);
    ~SynthRgn(void);

    SynthArt *AddArt(void);
    SynthArt *AddArt(std::vector<SynthConnectionBlock *> connBlocks);
    SynthSampInfo *AddSampInfo(void);
    SynthSampInfo *AddSampInfo(SynthSampInfo wsmp);
    void SetRanges(uint16_t keyLow = 0, uint16_t keyHigh = 0x7F, uint16_t velLow = 0,
                   uint16_t velHigh = 0x7F);
    void SetWaveLinkInfo(uint16_t options, uint16_t phaseGroup, uint32_t theChannel,
                         uint32_t theTableIndex);

   public:
    uint16_t usKeyLow;
    uint16_t usKeyHigh;
    uint16_t usVelLow;
    uint16_t usVelHigh;

    uint16_t fusOptions;
    uint16_t usPhaseGroup;
    uint32_t channel;
    uint32_t tableIndex;

    SynthSampInfo *sampinfo;
    SynthArt *art;
};

class SynthArt {
   public:
    SynthArt(void) {}
    SynthArt(std::vector<SynthConnectionBlock> &connectionBlocks);
    // SynthArt(uint16_t source, uint16_t control, uint16_t destination, uint16_t transform);
    ~SynthArt(void);

    void AddADSR(double attack, Transform atk_transform, double decay, double sustain_lev,
                 double sustain_time, double release_time, Transform rls_transform);
    void AddPan(double pan);

    double pan;  // -100% = left channel 100% = right channel 0 = 50/50

    double attack_time;  // rate expressed as seconds from 0 to 100% level
    double decay_time;   // rate expressed as seconds from 100% to 0% level, even though the sustain
                         // level isn't necessarily 0%
    double sustain_lev;  // db of attenuation at sustain level
    double sustain_time;  // this is part of the PSX envelope (and can actually be positive), but is
                          // not in DLS or SF2.  from 100 to 0, like release
    double release_time;  // rate expressed as seconds from 100% to 0% level, even though the
                          // sustain level may not be 100%
    Transform attack_transform;
    Transform release_transform;

   private:
    // vector<SynthConnectionBlock*> vConnBlocks;
};

class SynthSampInfo {
   public:
    SynthSampInfo(void) {}
    SynthSampInfo(uint16_t unityNote, int16_t fineTune, double atten, int8_t sampleLoops,
                  uint32_t loopType, uint32_t loopStart, uint32_t loopLength)
        : usUnityNote(unityNote),
          sFineTune(fineTune),
          attenuation(atten),
          cSampleLoops(sampleLoops),
          ulLoopType(loopType),
          ulLoopStart(loopStart),
          ulLoopLength(loopLength) {}
    ~SynthSampInfo(void) {}

    void SetLoopInfo(Loop &loop, VGMSamp *samp);
    // void SetPitchInfo(uint16_t unityNote, int16_t fineTune, double attenuation);
    void SetPitchInfo(uint16_t unityNote, int16_t fineTune, double attenuation);

   public:
    uint16_t usUnityNote;
    int16_t sFineTune;
    double attenuation;  // in decibels.
    int8_t cSampleLoops;

    uint32_t ulLoopType;
    uint32_t ulLoopStart;
    uint32_t ulLoopLength;
};

class SynthWave {
   public:
    SynthWave(void) : sampinfo(NULL), data(NULL), name("Untitled Wave") {
        RiffFile::AlignName(name);
    }
    SynthWave(uint16_t formatTag, uint16_t channels, int samplesPerSec, int aveBytesPerSec,
              uint16_t blockAlign, uint16_t bitsPerSample, uint32_t waveDataSize, uint8_t *waveData,
              std::string waveName = "Untitled Wave")
        : wFormatTag(formatTag),
          wChannels(channels),
          dwSamplesPerSec(samplesPerSec),
          dwAveBytesPerSec(aveBytesPerSec),
          wBlockAlign(blockAlign),
          wBitsPerSample(bitsPerSample),
          dataSize(waveDataSize),
          data(waveData),
          sampinfo(NULL),
          name(waveName) {
        RiffFile::AlignName(name);
    }
    ~SynthWave(void);

    SynthSampInfo *AddSampInfo(void);

    void ConvertTo16bitSigned();

   public:
    SynthSampInfo *sampinfo;

    uint16_t wFormatTag;
    uint16_t wChannels;
    uint32_t dwSamplesPerSec;
    uint32_t dwAveBytesPerSec;
    uint16_t wBlockAlign;
    uint16_t wBitsPerSample;

    uint32_t dataSize;
    uint8_t *data;

    std::string name;
};
