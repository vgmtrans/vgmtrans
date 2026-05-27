#pragma once
#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "SonyPS2Format.h"

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
    u32 splitBlockAddr;
    u8 nSplit;
    u8 sizeSplitBlock;
    u8 progVolume;
    s8 progPanpot;
    s8 progTranspose;
    s8 progDetune;
    s8 keyFollowPan;
    u8 keyFollowPanCenter;
    u8 progAttr;
    u8 reserved;
    u8 progLfoWave;
    u8 progLfoWave2;
    u8 progLfoStartPhase;
    u8 progLfoStartPhase2;
    u8 progLfoPhaseRandom;
    u8 progLfoPhaseRandom2;
    u16 progLfoCycle;
    u16 progLfoCycle2;
    s16 progLfoPitchDepth;
    s16 progLfoPitchDepth2;
    s16 progLfoMidiPitchDepth;
    s16 progLfoMidiPitchDepth2;
    s8 progLfoAmpDepth;
    s8 progLfoAmpDepth2;
    s8 progLfoMidiAmpDepth;
    s8 progLfoMidiAmpDepth2;
  } ProgParam;

  typedef struct _SplitBlock {
    u16 sampleSetIndex;
    u8 splitRangeLow;
    u8 splitCrossFade;
    u8 splitRangeHigh;
    u8 splitNumber;
    u16 splitBendRangeLow;
    u16 SplitBendRangeHigh;
    s8 keyFollowPitch;
    u8 keyFollowPitchCenter;
    s8 keyFollowAmp;
    u8 keyFollowAmpCenter;
    s8 keyFollowPan;
    u8 keyFollowPanCenter;
    u8 splitVolume;
    s8 splitPanpot;
    s8 splitTranspose;
    s8 splitDetune;
  } SplitBlock;

  SonyPS2Instr(VGMInstrSet *instrSet, u32 offset, u32 length, u32 theBank, u32 theInstrNum);
  ~SonyPS2Instr() override;

  bool loadInstr() override;
  s8 convertPanValue(u8 panVal);

 public:
  SplitBlock *splitBlocks{};
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
    u32 Creator;
    u32 Type;
    u32 chunkSize;
    u32 fileSize;
    u32 bodySize;
    u32 programChunkAddr;
    u32 samplesetChunkAddr;
    u32 sampleChunkAddr;
    u32 vagInfoChunkAddr;
    u32 seTimbreChunkAddr;
    u32 reserved[8];
  } HdrCk;

  typedef struct _ProgCk {
    _ProgCk() : programOffsetAddr(0), progParamBlock(0) { }
    ~_ProgCk() {
      if (programOffsetAddr) delete[] programOffsetAddr;
      if (progParamBlock) delete[] progParamBlock;
    }
    u32 Creator;
    u32 Type;
    u32 chunkSize;
    u32 maxProgramNumber;
    u32 *programOffsetAddr;
    SonyPS2Instr::ProgParam *progParamBlock;
  } ProgCk;

  typedef struct _SampSetParam {
    _SampSetParam() : sampleIndex(0) { }
    ~_SampSetParam() { if (sampleIndex) delete[] sampleIndex; }
    u8 velCurve;
    u8 velLimitLow;
    u8 velLimitHigh;
    u8 nSample;
    u16 *sampleIndex;
  } SampSetParam;

  typedef struct _SampSetCk {
    _SampSetCk() : sampleSetOffsetAddr(0), sampleSetParam(0) { }
    ~_SampSetCk() {
      if (sampleSetOffsetAddr) delete[] sampleSetOffsetAddr;
      if (sampleSetParam) delete[] sampleSetParam;
    }
    u32 Creator;
    u32 Type;
    u32 chunkSize;
    u32 maxSampleSetNumber;
    u32 *sampleSetOffsetAddr;
    SampSetParam *sampleSetParam;
  } SampSetCk;

  typedef struct _SampleParam {
    u16 VagIndex;
    u8 velRangeLow;
    u8 velCrossFade;
    u8 velRangeHigh;
    s8 velFollowPitch;
    u8 velFollowPitchCenter;
    u8 velFollowPitchVelCurve;
    s8 velFollowAmp;
    u8 velFollowAmpCenter;
    u8 velFollowAmpVelCurve;
    u8 sampleBaseNote;
    s8 sampleDetune;
    s8 samplePanpot;
    u8 sampleGroup;
    u8 samplePriority;
    u8 sampleVolume;
    u8 reserved;
    u16 sampleAdsr1;
    u16 sampleAdsr2;
    s8 keyFollowAr;
    u8 keyFollowArCenter;
    s8 keyFollowDr;
    u8 keyFollowDrCenter;
    s8 keyFollowSr;
    u8 keyFollowSrCenter;
    s8 keyFollowRr;
    u8 keyFollowRrCenter;
    s8 keyFollowSl;
    u8 keyFollowSlCenter;
    u16 samplePitchLfoDelay;
    u16 samplePitchLfoFade;
    u16 sampleAmpLfoDelay;
    u16 sampleAmpLfoFade;
    u8 sampleLfoAttr;
    u8 sampleSpuAttr;
  } SampleParam;

  typedef struct _SampCk {
    _SampCk() : sampleOffsetAddr(0), sampleParam(0) { }
    ~_SampCk() {
      if (sampleOffsetAddr) delete[] sampleOffsetAddr;
      if (sampleParam) delete[] sampleParam;
    }
    u32 Creator;
    u32 Type;
    u32 chunkSize;
    u32 maxSampleNumber;
    u32 *sampleOffsetAddr;
    SampleParam *sampleParam;
  } SampCk;

  typedef struct _VAGInfoParam {
    u32 vagOffsetAddr;
    u16 vagSampleRate;
    u8 vagAttribute;
    u8 reserved;
  } VAGInfoParam;

  typedef struct _VAGInfoCk {
    _VAGInfoCk() : vagInfoOffsetAddr(0), vagInfoParam(0) { }
    ~_VAGInfoCk() {
      if (vagInfoOffsetAddr) delete[] vagInfoOffsetAddr;
      if (vagInfoParam) delete[] vagInfoParam;
    }
    u32 Creator;
    u32 Type;
    u32 chunkSize;
    u32 maxVagInfoNumber;
    u32 *vagInfoOffsetAddr;
    VAGInfoParam *vagInfoParam;
  } VAGInfoCk;

  SonyPS2InstrSet(RawFile *file, u32 offset);
  ~SonyPS2InstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

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
  SonyPS2SampColl(RawFile *file, u32 offset, u32 length = 0);

  bool parseSampleInfo() override;
};
