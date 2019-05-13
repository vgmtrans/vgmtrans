/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "common.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "SonyPS2Format.h"

//FORWARD_DECLARE_TYPEDEF_STRUCT(ProgParam);


#define SCEHD_LFO_NON        0
#define SCEHD_LFO_SAWUP      1
#define SCEHD_LFO_SAWDOWN    2
#define SCEHD_LFO_TRIANGLE   3
#define SCEHD_LFO_SQUEARE    4
#define SCEHD_LFO_NOISE      5
#define SCEHD_LFO_SIN        6
#define SCEHD_LFO_USER       0x80

#define SCEHD_CROSSFADE      0x80


#define SCEHD_LFO_PITCH_KEYON   0x01
#define SCEHD_LFO_PITCH_KEYOFF  0x02
#define SCEHD_LFO_PITCH_BOTH    0x04
#define SCEHD_LFO_AMP_KEYON     0x10
#define SCEHD_LFO_AMP_KEYOFF    0x20
#define SCEHD_LFO_AMP_BOTH      0x40

#define SCEHD_SPU_DIRECTSEND_L  0x01
#define SCEHD_SPU_DIRECTSEND_R  0x02
#define SCEHD_SPU_EFFECTSEND_L  0x04
#define SCEHD_SPU_EFFECTSEND_R  0x08
#define SCEHD_SPU_CORE_0        0x10
#define SCEHD_SPU_CORE_1        0x20

#define SCEHD_VAG_1SHOT         0
#define SCEHD_VAG_LOOP          1


// ************
// SonyPS2Instr
// ************

class SonyPS2Instr
    : public VGMInstr {
  friend class SonyPS2InstrSet;
 public:

  typedef struct _ProgParam {
    uint32_t splitBlockAddr;
    uint8_t nSplit;
    uint8_t sizeSplitBlock;
    uint8_t progVolume;
    int8_t progPanpot;
    int8_t progTranspose;
    int8_t progDetune;
    int8_t keyFollowPan;
    uint8_t keyFollowPanCenter;
    uint8_t progAttr;
    uint8_t reserved;
    uint8_t progLfoWave;
    uint8_t progLfoWave2;
    uint8_t progLfoStartPhase;
    uint8_t progLfoStartPhase2;
    uint8_t progLfoPhaseRandom;
    uint8_t progLfoPhaseRandom2;
    uint16_t progLfoCycle;
    uint16_t progLfoCycle2;
    int16_t progLfoPitchDepth;
    int16_t progLfoPitchDepth2;
    int16_t progLfoMidiPitchDepth;
    int16_t progLfoMidiPitchDepth2;
    int8_t progLfoAmpDepth;
    int8_t progLfoAmpDepth2;
    int8_t progLfoMidiAmpDepth;
    int8_t progLfoMidiAmpDepth2;
  } ProgParam;

  typedef struct _SplitBlock {
    uint16_t sampleSetIndex;
    uint8_t splitRangeLow;
    uint8_t splitCrossFade;
    uint8_t splitRangeHigh;
    uint8_t splitNumber;
    uint16_t splitBendRangeLow;
    uint16_t SplitBendRangeHigh;
    int8_t keyFollowPitch;
    uint8_t keyFollowPitchCenter;
    int8_t keyFollowAmp;
    uint8_t keyFollowAmpCenter;
    int8_t keyFollowPan;
    uint8_t keyFollowPanCenter;
    uint8_t splitVolume;
    int8_t splitPanpot;
    int8_t splitTranspose;
    int8_t splitDetune;
  } SplitBlock;

  SonyPS2Instr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank, uint32_t theInstrNum);
  virtual ~SonyPS2Instr(void);

  virtual bool LoadInstr();
  int8_t ConvertPanVal(uint8_t panVal);

 public:
  SplitBlock *splitBlocks;
};



// ***************
// SonyPS2InstrSet
// ***************

class SonyPS2InstrSet
    : public VGMInstrSet {
  friend class SonyPS2SampColl;
  friend class SonyPS2Instr;

 public:
  typedef struct _HdrCk {
    uint32_t Creator;
    uint32_t Type;
    uint32_t chunkSize;
    uint32_t fileSize;
    uint32_t bodySize;
    uint32_t programChunkAddr;
    uint32_t samplesetChunkAddr;
    uint32_t sampleChunkAddr;
    uint32_t vagInfoChunkAddr;
    uint32_t seTimbreChunkAddr;
    uint32_t reserved[8];
  } HdrCk;

  typedef struct _ProgCk {
    _ProgCk() : programOffsetAddr(0), progParamBlock(0) { }
    ~_ProgCk() {
      if (programOffsetAddr) delete[] programOffsetAddr;
      if (progParamBlock) delete[] progParamBlock;
    }
    uint32_t Creator;
    uint32_t Type;
    uint32_t chunkSize;
    uint32_t maxProgramNumber;
    uint32_t *programOffsetAddr;
    SonyPS2Instr::ProgParam *progParamBlock;
  } ProgCk;

  typedef struct _SampSetParam {
    _SampSetParam() : sampleIndex(0) { }
    ~_SampSetParam() { if (sampleIndex) delete[] sampleIndex; }
    uint8_t velCurve;
    uint8_t velLimitLow;
    uint8_t velLimitHigh;
    uint8_t nSample;
    uint16_t *sampleIndex;
  } SampSetParam;

  typedef struct _SampSetCk {
    _SampSetCk() : sampleSetOffsetAddr(0), sampleSetParam(0) { }
    ~_SampSetCk() {
      if (sampleSetOffsetAddr) delete[] sampleSetOffsetAddr;
      if (sampleSetParam) delete[] sampleSetParam;
    }
    uint32_t Creator;
    uint32_t Type;
    uint32_t chunkSize;
    uint32_t maxSampleSetNumber;
    uint32_t *sampleSetOffsetAddr;
    SampSetParam *sampleSetParam;
  } SampSetCk;

  typedef struct _SampleParam {
    uint16_t VagIndex;
    uint8_t velRangeLow;
    uint8_t velCrossFade;
    uint8_t velRangeHigh;
    int8_t velFollowPitch;
    uint8_t velFollowPitchCenter;
    uint8_t velFollowPitchVelCurve;
    int8_t velFollowAmp;
    uint8_t velFollowAmpCenter;
    uint8_t velFollowAmpVelCurve;
    uint8_t sampleBaseNote;
    int8_t sampleDetune;
    int8_t samplePanpot;
    uint8_t sampleGroup;
    uint8_t samplePriority;
    uint8_t sampleVolume;
    uint8_t reserved;
    uint16_t sampleAdsr1;
    uint16_t sampleAdsr2;
    int8_t keyFollowAr;
    uint8_t keyFollowArCenter;
    int8_t keyFollowDr;
    uint8_t keyFollowDrCenter;
    int8_t keyFollowSr;
    uint8_t keyFollowSrCenter;
    int8_t keyFollowRr;
    uint8_t keyFollowRrCenter;
    int8_t keyFollowSl;
    uint8_t keyFollowSlCenter;
    uint16_t samplePitchLfoDelay;
    uint16_t samplePitchLfoFade;
    uint16_t sampleAmpLfoDelay;
    uint16_t sampleAmpLfoFade;
    uint8_t sampleLfoAttr;
    uint8_t sampleSpuAttr;
  } SampleParam;

  typedef struct _SampCk {
    _SampCk() : sampleOffsetAddr(0), sampleParam(0) { }
    ~_SampCk() {
      if (sampleOffsetAddr) delete[] sampleOffsetAddr;
      if (sampleParam) delete[] sampleParam;
    }
    uint32_t Creator;
    uint32_t Type;
    uint32_t chunkSize;
    uint32_t maxSampleNumber;
    uint32_t *sampleOffsetAddr;
    SampleParam *sampleParam;
  } SampCk;

  typedef struct _VAGInfoParam {
    uint32_t vagOffsetAddr;
    uint16_t vagSampleRate;
    uint8_t vagAttribute;
    uint8_t reserved;
  } VAGInfoParam;

  typedef struct _VAGInfoCk {
    _VAGInfoCk() : vagInfoOffsetAddr(0), vagInfoParam(0) { }
    ~_VAGInfoCk() {
      if (vagInfoOffsetAddr) delete[] vagInfoOffsetAddr;
      if (vagInfoParam) delete[] vagInfoParam;
    }
    uint32_t Creator;
    uint32_t Type;
    uint32_t chunkSize;
    uint32_t maxVagInfoNumber;
    uint32_t *vagInfoOffsetAddr;
    VAGInfoParam *vagInfoParam;
  } VAGInfoCk;

  SonyPS2InstrSet(RawFile *file, uint32_t offset);
  virtual ~SonyPS2InstrSet(void);

  virtual bool GetHeaderInfo();
  virtual bool GetInstrPointers();

 protected:
  VersCk versCk;
  HdrCk hdrCk;
  ProgCk progCk;
  SampSetParam sampSetParam;
  SampSetCk sampSetCk;
  SampCk sampCk;
  VAGInfoCk vagInfoCk;
};

// ***************
// SonyPS2SampColl
// ***************

class SonyPS2SampColl
    : public VGMSampColl {
 public:
  SonyPS2SampColl(RawFile *file, uint32_t offset, uint32_t length = 0);

  virtual bool GetSampleInfo();
};
