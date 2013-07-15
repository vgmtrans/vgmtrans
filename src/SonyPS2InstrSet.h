#pragma once
#include "common.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "SonyPS2Format.h"

//FORWARD_DECLARE_TYPEDEF_STRUCT(ProgParam);


#define SCEHD_LFO_NON		0 
#define SCEHD_LFO_SAWUP		1 
#define SCEHD_LFO_SAWDOWN	2 
#define SCEHD_LFO_TRIANGLE  3 
#define SCEHD_LFO_SQUEARE	4 
#define SCEHD_LFO_NOISE		5 
#define SCEHD_LFO_SIN		6 
#define SCEHD_LFO_USER		0x80 

#define SCEHD_CROSSFADE		0x80 


#define SCEHD_LFO_PITCH_KEYON	0x01 
#define SCEHD_LFO_PITCH_KEYOFF  0x02 
#define SCEHD_LFO_PITCH_BOTH	0x04 
#define SCEHD_LFO_AMP_KEYON		0x10 
#define SCEHD_LFO_AMP_KEYOFF	0x20 
#define SCEHD_LFO_AMP_BOTH		0x40 

#define SCEHD_SPU_DIRECTSEND_L  0x01 
#define SCEHD_SPU_DIRECTSEND_R  0x02 
#define SCEHD_SPU_EFFECTSEND_L  0x04 
#define SCEHD_SPU_EFFECTSEND_R  0x08 
#define SCEHD_SPU_CORE_0		0x10 
#define SCEHD_SPU_CORE_1		0x20 

#define SCEHD_VAG_1SHOT			0
#define SCEHD_VAG_LOOP			1


// ************
// SonyPS2Instr
// ************

class SonyPS2Instr
	: public VGMInstr
{
	friend class SonyPS2InstrSet;
public:

	typedef struct _ProgParam
	{
		U32 splitBlockAddr;
		U8 nSplit;
		U8 sizeSplitBlock;
		U8 progVolume;
		S8 progPanpot;
		S8 progTranspose;
		S8 progDetune;
		S8 keyFollowPan;
		U8 keyFollowPanCenter;
		U8 progAttr;
		U8 reserved;
		U8 progLfoWave;
		U8 progLfoWave2;
		U8 progLfoStartPhase;
		U8 progLfoStartPhase2;
		U8 progLfoPhaseRandom;
		U8 progLfoPhaseRandom2;
		U16 progLfoCycle;
		U16 progLfoCycle2;
		S16 progLfoPitchDepth;
		S16 progLfoPitchDepth2;
		S16 progLfoMidiPitchDepth;
		S16 progLfoMidiPitchDepth2;
		S8 progLfoAmpDepth;
		S8 progLfoAmpDepth2;
		S8 progLfoMidiAmpDepth;
		S8 progLfoMidiAmpDepth2;
	} ProgParam;

	typedef struct _SplitBlock
	{
		U16 sampleSetIndex;
		U8 splitRangeLow;
		U8 splitCrossFade;
		U8 splitRangeHigh;
		U8 splitNumber;
		U16 splitBendRangeLow; 
		U16 SplitBendRangeHigh;
		S8 keyFollowPitch;
		U8 keyFollowPitchCenter;
		S8 keyFollowAmp;
		U8 keyFollowAmpCenter;
		S8 keyFollowPan;
		U8 keyFollowPanCenter;
		U8 splitVolume;
		S8 splitPanpot;
		S8 splitTranspose;
		S8 splitDetune;
	} SplitBlock;

	SonyPS2Instr(VGMInstrSet* instrSet, ULONG offset, ULONG length, ULONG theBank, ULONG theInstrNum);
	virtual ~SonyPS2Instr(void);

	virtual int LoadInstr();
	S8 ConvertPanVal(U8 panVal);

public:
	SplitBlock* splitBlocks;
};



// ***************
// SonyPS2InstrSet
// ***************

class SonyPS2InstrSet
	: public VGMInstrSet
{
	friend class SonyPS2SampColl;
	friend class SonyPS2Instr;

public:
	typedef struct _HdrCk
	{
		U32 Creator;
		U32 Type;
		U32 chunkSize;
		U32 fileSize;
		U32	bodySize;
		U32 programChunkAddr;
		U32 samplesetChunkAddr;
		U32 sampleChunkAddr;
		U32 vagInfoChunkAddr;
		U32 seTimbreChunkAddr;
		U32 reserved[8];
	} HdrCk;

	typedef struct _ProgCk
	{
		_ProgCk() : programOffsetAddr(0), progParamBlock(0) {}
		~_ProgCk() { if (programOffsetAddr) delete[] programOffsetAddr;
		             if (progParamBlock) delete[] progParamBlock;}
		U32 Creator;
		U32 Type;
		U32 chunkSize;
		U32 maxProgramNumber;
		U32* programOffsetAddr;
		SonyPS2Instr::ProgParam* progParamBlock;
	} ProgCk;

	typedef struct _SampSetParam
	{
		_SampSetParam() : sampleIndex(0) {}
		~_SampSetParam() { if (sampleIndex) delete[] sampleIndex; }
		U8 velCurve;
		U8 velLimitLow;
		U8 velLimitHigh;
		U8 nSample;
		U16* sampleIndex;
	} SampSetParam;

	typedef struct _SampSetCk
	{
		_SampSetCk() : sampleSetOffsetAddr(0), sampleSetParam(0) {}
		~_SampSetCk() { if (sampleSetOffsetAddr) delete[] sampleSetOffsetAddr;
		                if (sampleSetParam) delete[] sampleSetParam; }
		U32 Creator;
		U32 Type;
		U32 chunkSize;
		U32 maxSampleSetNumber;
		U32* sampleSetOffsetAddr;
		SampSetParam* sampleSetParam;
	} SampSetCk;

	typedef struct _SampleParam
	{
		U16 VagIndex;
		U8 velRangeLow;
		U8 velCrossFade;
		U8 velRangeHigh;
		S8 velFollowPitch;
		U8 velFollowPitchCenter;
		U8 velFollowPitchVelCurve;
		S8 velFollowAmp;
		U8 velFollowAmpCenter; 
		U8 velFollowAmpVelCurve;
		U8 sampleBaseNote;
		S8 sampleDetune; 
		S8 samplePanpot;
		U8 sampleGroup;
		U8 samplePriority;
		U8 sampleVolume;
		U8 reserved;
		U16 sampleAdsr1;
		U16 sampleAdsr2;
		S8 keyFollowAr;
		U8 keyFollowArCenter;
		S8 keyFollowDr;
		U8 keyFollowDrCenter;
		S8 keyFollowSr;
		U8 keyFollowSrCenter;
		S8 keyFollowRr;
		U8 keyFollowRrCenter;
		S8 keyFollowSl;
		U8 keyFollowSlCenter;
		U16 samplePitchLfoDelay;
		U16 samplePitchLfoFade;
		U16 sampleAmpLfoDelay;
		U16 sampleAmpLfoFade;
		U8 sampleLfoAttr;
		U8 sampleSpuAttr;
	} SampleParam;

	typedef struct _SampCk
	{
		_SampCk() : sampleOffsetAddr(0), sampleParam(0) {}
		~_SampCk() { if (sampleOffsetAddr) delete[] sampleOffsetAddr;
		             if (sampleParam) delete[] sampleParam; }
		U32 Creator;
		U32 Type;
		U32 chunkSize;
		U32 maxSampleNumber;
		U32* sampleOffsetAddr;
		SampleParam* sampleParam;
	} SampCk;

	typedef struct _VAGInfoParam
	{
		U32 vagOffsetAddr;
		U16 vagSampleRate;
		U8 vagAttribute;
		U8 reserved;
	} VAGInfoParam;

	typedef struct _VAGInfoCk
	{
		_VAGInfoCk() : vagInfoOffsetAddr(0), vagInfoParam(0) {}
		~_VAGInfoCk() { if (vagInfoOffsetAddr) delete[] vagInfoOffsetAddr;
		                if (vagInfoParam) delete[] vagInfoParam; }
		U32 Creator;
		U32 Type;
		U32 chunkSize;
		U32 maxVagInfoNumber;
		U32* vagInfoOffsetAddr;
		VAGInfoParam* vagInfoParam;
	} VAGInfoCk;

	SonyPS2InstrSet(RawFile* file, ULONG offset);
	virtual ~SonyPS2InstrSet(void);

	virtual int GetHeaderInfo();
	virtual int GetInstrPointers();

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
	: public VGMSampColl
{
public:
	SonyPS2SampColl(RawFile* file, ULONG offset, ULONG length = 0);

	virtual bool GetSampleInfo();		//retrieve sample info, including pointers to data, # channels, rate, etc.
};
