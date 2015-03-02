#pragma once

#include "common.h"
#include "RiffFile.h"

typedef enum : uint16_t
{       
    // Oscillator
    startAddrsOffset,       //sample start address -4 (0 to 0xffffff)   0
    endAddrsOffset,
    startloopAddrsOffset,   //loop start address -4 (0 to 0xffffff)
    endloopAddrsOffset,     //loop end address -3 (0 to 0xffffff)

    // Pitch
    startAddrsCoarseOffset, //CHANGED FOR SF2
    modLfoToPitch,          //main fm: lfo1-> pitch                     5
    vibLfoToPitch,          //aux fm:  lfo2-> pitch
    modEnvToPitch,          //pitch env: env1(aux)-> pitch

    // Filter
    initialFilterFc,        //initial filter cutoff
    initialFilterQ,         //filter Q
    modLfoToFilterFc,       //filter modulation: lfo1 -> filter cutoff  10
    modEnvToFilterFc,       //filter env: env1(aux)-> filter cutoff
        
    // Amplifier
    endAddrsCoarseOffset,   //CHANGED FOR SF2
    modLfoToVolume,         //tremolo: lfo1-> volume
    unused1,

    // Effects
    chorusEffectsSend,      //chorus                                    15
    reverbEffectsSend,      //reverb
    pan,
    unused2,
    unused3,
    unused4,                //                                          20

    // Main lfo1
    delayModLFO,            //delay 0x8000-n*(725us)
    freqModLFO,             //frequency
        
    // Aux lfo2
    delayVibLFO,            //delay 0x8000-n*(725us)
    freqVibLFO,             //frequency

    // Env1(aux/value)
    delayModEnv,            //delay 0x8000 - n(725us)                   25
    attackModEnv,           //attack
    holdModEnv,             //hold
    decayModEnv,            //decay
    sustainModEnv,          //sustain
    releaseModEnv,          //release                                   30
    keynumToModEnvHold,
    keynumToModEnvDecay,

    // Env2(ampl/vol)
    delayVolEnv,            //delay 0x8000 - n(725us)
    attackVolEnv,           //attack
    holdVolEnv,             //hold                                      35
    decayVolEnv,            //decay
    sustainVolEnv,          //sustain
    releaseVolEnv,          //release
    keynumToVolEnvHold,
    keynumToVolEnvDecay,    //                                          40

    // Preset
    instrument,
    reserved1,
    keyRange,
    velRange,
    startloopAddrCoarseOffset, //CHANGED FOR SF2                       45
    keynum,
    velocity,
    initialAttenuation,            //CHANGED FOR SF2
    reserved2,
    endloopAddrsCoarseOffset,   //CHANGED FOR SF2                       50
    coarseTune,
    fineTune,
    sampleID,
    sampleModes,                //CHANGED FOR SF2
    reserved3,                  //                                      55
    scaleTuning,
    exclusiveClass,
    overridingRootKey,
    unused5,
    endOper                     //                                      60
} SFGenerator;

typedef enum : uint16_t
{		
    /* Start of MIDI modulation operators */
    cc1_Mod,
    cc7_Vol,
    cc10_Pan,
    cc64_Sustain,
    cc91_Reverb,
    cc93_Chorus,

    ccPitchBend,
    ccIndirectModX,
    ccIndirectModY,

    endMod
} SFModulator;

typedef enum : uint16_t
{
	linear
} SFTransform;



/*
#define monoSample      0x0001
#define rightSample     0x0002
#define leftSample      0x0004
#define linkedSample    0x0008

#define ROMSample       0x8000          //32768
#define ROMMonoSample   0x8001          //32769
#define ROMRightSample  0x8002          //32770
#define ROMLeftSample   0x8004          //32772
#define ROMLinkedSample 0x8008          //32776
*/

//enum scaleTuning 
//{
//    equalTemp,
//    fiftyCents
//};
//
//enum SFSampleField          //used by Sample Read Module
//{
//    NAME_FIELD = 1,
//    START_FIELD,
//    END_FIELD,
//    START_LOOP_FIELD,
//    END_LOOP_FIELD,
//    SMPL_RATE_FIELD,
//    ORG_KEY_FIELD,
//    CORRECTION_FIELD,
//    SMPL_LINK_FIELD,
//    SMPL_TYPE_FIELD
//};
//
//enum SFInfoChunkField       //used by Bank Read Module
//{
//    IFIL_FIELD = 1,
//    IROM_FIELD,
//    IVER_FIELD,
//    ISNG_FIELD,
//    INAM_FIELD,
//    IPRD_FIELD,
//    IENG_FIELD,
//    ISFT_FIELD,
//    ICRD_FIELD,
//    ICMT_FIELD,
//    ICOP_FIELD
//};


#pragma pack(push)  /* push current alignment to stack */
#pragma pack(2)     /* set alignment to 2 byte boundary */

struct sfVersionTag
{
	uint16_t wMajor;
	uint16_t wMinor;
};

struct sfPresetHeader
{
	CHAR achPresetName[20];
	uint16_t wPreset;
	uint16_t wBank;
	uint16_t wPresetBagNdx;
	uint32_t dwLibrary;
	uint32_t dwGenre;
	uint32_t dwMorphology;
};

struct sfPresetBag
{
	uint16_t wGenNdx;
	uint16_t wModNdx;
};

struct sfModList
{
	SFModulator sfModSrcOper;
	SFGenerator sfModDestOper;
	int16_t modAmount;
	SFModulator sfModAmtSrcOper;
	SFTransform sfModTransOper;
};

typedef struct
{
	uint8_t byLo;
	uint8_t byHi;
} rangesType;
	
typedef union
{
	rangesType ranges;
	int16_t shAmount;
	uint16_t wAmount;
} genAmountType;

struct sfGenList
{
	SFGenerator sfGenOper;
	genAmountType genAmount;
};

struct sfInstModList
{
	SFModulator sfModSrcOper;
	SFGenerator sfModDestOper;
	int16_t modAmount;
	SFModulator sfModAmtSrcOper;
	SFTransform sfModTransOper;
};

struct sfInstGenList
{
	SFGenerator sfGenOper;
	genAmountType genAmount;
};

struct sfInst
{
	CHAR achInstName[20];
	uint16_t wInstBagNdx;
};

struct sfInstBag
{
	uint16_t wInstGenNdx;
	uint16_t wInstModNdx;
};

typedef enum : uint16_t
{
	monoSample = 1,
	rightSample = 2,
	leftSample = 4,
	linkedSample = 8,
	RomMonoSample = 0x8001,
	RomRightSample = 0x8002,
	RomLeftSample = 0x8004,
	RomLinkedSample = 0x8008
} SFSampleLink;

struct sfSample
{
	CHAR achSampleName[20];
	uint32_t dwStart;
	uint32_t dwEnd;
	uint32_t dwStartloop;
	uint32_t dwEndloop;
	uint32_t dwSampleRate;
	uint8_t byOriginalKey;
	CHAR chCorrection;
	uint16_t wSampleLink;
	SFSampleLink sfSampleType;
};

#pragma pack(pop)   /* restore original alignment from stack */



class SF2StringChunk : public Chunk
{
public:
	SF2StringChunk(std::string ckSig, std::string info)
		: Chunk(ckSig)
	{
		SetData(info.c_str(), (uint32_t)info.length());
	}
};

class SF2InfoListChunk : public LISTChunk
{
public:
	SF2InfoListChunk(std::string name);
};


class SF2sdtaChunk : public LISTChunk
{
public:
	SF2sdtaChunk();
};

//class SF2smplChunk : public Chunk
//{
//public:
//
//};




//#define COLH_SIZE (4+8)
//#define INSH_SIZE (12+8)
//#define RGNH_SIZE (14+8)//(12+8)
//#define WLNK_SIZE (12+8)
//#define LIST_HDR_SIZE 12

//class DLSInstr;
//class DLSRgn;
//class DLSArt;
//class connectionBlock;
//class DLSWsmp;
//class DLSWave;

inline void WriteLIST(std::vector<uint8_t> &buf, std::string listName, uint32_t listSize);
inline void AlignName(std::string& name);

class SynthFile;

class SF2File : public RiffFile
{
public:
	//SF2File(wstring sf2_name = L"Instrument Set");
	SF2File(SynthFile* synthfile);
	~SF2File(void);

	//static SF2File* ConvertToSF2(VGMColl* coll);

	//SF2File* AddInstr(unsigned long bank, unsigned long instrNum);
	//SF2File* AddInstr(unsigned long bank, unsigned long instrNum, string Name);
	//void DeleteInstr(unsigned long bank, unsigned long instrNum);
	//DLSWave* AddWave(uint16_t formatTag, uint16_t channels, int samplesPerSec, int aveBytesPerSec,
	//				 uint16_t blockAlign, uint16_t bitsPerSample, uint32_t waveDataSize, uint8_t* waveData,
	//				 string name = "Unnamed Wave");
	//void SetName(string dls_name);

	//uint32_t GetSize(void);

	//int WriteDLSToBuffer(vector<uint8_t> &buf);
	bool SaveSF2File(const std::wstring & filepath);

public:
	//vector<DLSInstr*> aInstrs;
	//vector<DLSWave*> aWaves; 

};
