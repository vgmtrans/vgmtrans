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

typedef enum {
	no_transform,
	concave_transform
} Transform;

class SynthFile
{
public:
	SynthFile(const std::string synth_name = "Instrument Set");
	~SynthFile(void);

	SynthInstr* AddInstr(unsigned long bank, unsigned long instrNum);
	SynthInstr* AddInstr(unsigned long bank, unsigned long instrNum, std::string Name);
	void DeleteInstr(unsigned long bank, unsigned long instrNum);
	SynthWave* AddWave(USHORT formatTag, USHORT channels, int samplesPerSec, int aveBytesPerSec,
					 USHORT blockAlign, USHORT bitsPerSample, ULONG waveDataSize, BYTE* waveData,
					 std::string name = "Unnamed Wave");
	void SetName(std::string synth_name);

	//int WriteDLSToBuffer(std::vector<BYTE> &buf);
	//bool SaveDLSFile(const wchar_t* filepath);

public:
	std::vector<SynthInstr*> vInstrs;
	std::vector<SynthWave*> vWaves; 
	std::string name;
};

class SynthInstr
{
public:
	SynthInstr(void);
	SynthInstr(ULONG bank, ULONG instrument);
	SynthInstr(ULONG bank, ULONG instrument, std::string instrName);
	SynthInstr(ULONG bank, ULONG instrument, std::string instrName, std::vector<SynthRgn*> listRgns);
	~SynthInstr(void);

	void AddRgnList(std::vector<SynthRgn>& RgnList);
	SynthRgn* AddRgn(void);
	SynthRgn* AddRgn(SynthRgn rgn);

public:
	ULONG ulBank;
	ULONG ulInstrument;
 
	std::vector<SynthRgn*> vRgns;
	std::string name;

};

class SynthRgn
{
public:
	SynthRgn(void): sampinfo(NULL), art(NULL) {}
	SynthRgn(USHORT keyLow, USHORT keyHigh, USHORT velLow, USHORT velHigh)
		: usKeyLow(keyLow), usKeyHigh(keyHigh), usVelLow(velLow), usVelHigh(velHigh), sampinfo(NULL), art(NULL) {}
	SynthRgn(USHORT keyLow, USHORT keyHigh, USHORT velLow, USHORT velHigh, SynthArt& art);
	~SynthRgn(void);

	SynthArt* AddArt(void);
	SynthArt* AddArt(std::vector<SynthConnectionBlock*> connBlocks);
	SynthSampInfo* AddSampInfo(void);
	SynthSampInfo* AddSampInfo(SynthSampInfo wsmp);
	void SetRanges(USHORT keyLow = 0, USHORT keyHigh = 0x7F, USHORT velLow = 0, USHORT velHigh = 0x7F);
	void SetWaveLinkInfo(USHORT options, USHORT phaseGroup, ULONG theChannel, ULONG theTableIndex);

public:
	USHORT usKeyLow;
	USHORT usKeyHigh;
	USHORT usVelLow;
	USHORT usVelHigh;

	USHORT fusOptions;
	USHORT usPhaseGroup;
	ULONG channel;
	ULONG tableIndex;

	SynthSampInfo* sampinfo;
	SynthArt* art;
};

class SynthArt
{
public:
	SynthArt(void) {}
	SynthArt(std::vector<SynthConnectionBlock>& connectionBlocks);
	//SynthArt(USHORT source, USHORT control, USHORT destination, USHORT transform);
	~SynthArt(void);

	void AddADSR(double attack, Transform atk_transform, double decay, double sustain_lev,
				 double sustain_time, double release_time, Transform rls_transform);
	void AddPan(double pan);

	double pan;				// -100% = left channel 100% = right channel 0 = 50/50

	double attack_time;		// rate expressed as seconds from 0 to 100% level
	double decay_time;		// rate expressed as seconds from 100% to 0% level, even though the sustain level isn't necessarily 0%
	double sustain_lev;		// db of attenuation at sustain level
	double sustain_time;	// this is part of the PSX envelope (and can actually be positive), but is not in DLS or SF2.  from 100 to 0, like release
	double release_time;	// rate expressed as seconds from 100% to 0% level, even though the sustain level may not be 100%
	Transform attack_transform;
	Transform release_transform;

private:
	//vector<SynthConnectionBlock*> vConnBlocks;
};

class SynthSampInfo
{
public:
	SynthSampInfo(void) {}
	SynthSampInfo(USHORT unityNote, SHORT fineTune, double atten, char sampleLoops, ULONG loopType, ULONG loopStart, ULONG loopLength)
		: usUnityNote(unityNote), sFineTune(fineTune), attenuation(atten), cSampleLoops(sampleLoops), ulLoopType(loopType),
		ulLoopStart(loopStart), ulLoopLength(loopLength) {}
	~SynthSampInfo(void) {}

	void SetLoopInfo(Loop& loop, VGMSamp* samp);
	//void SetPitchInfo(USHORT unityNote, short fineTune, double attenuation);
	void SetPitchInfo(USHORT unityNote, short fineTune, double attenuation);

public:
	unsigned short usUnityNote;
	short sFineTune;
	double attenuation;	// in decibels.
	char cSampleLoops;

	ULONG ulLoopType;
	ULONG ulLoopStart;
	ULONG ulLoopLength;
};

class SynthWave
{
public:
	SynthWave(void) 
		: sampinfo(NULL), 
		  data(NULL), 
		  name("Untitled Wave") 
	{ 
		RiffFile::AlignName(name); 
	}
	SynthWave(USHORT formatTag, USHORT channels, int samplesPerSec, int aveBytesPerSec, USHORT blockAlign,
		USHORT bitsPerSample, ULONG waveDataSize, unsigned char* waveData, std::string waveName = "Untitled Wave")
		: wFormatTag(formatTag),
		  wChannels(channels),
		  dwSamplesPerSec(samplesPerSec),
		  dwAveBytesPerSec(aveBytesPerSec),
		  wBlockAlign(blockAlign),
		  wBitsPerSample(bitsPerSample),
		  dataSize(waveDataSize),
		  data(waveData),
		  sampinfo(NULL),
		  name(waveName)
	{
		RiffFile::AlignName(name);
	}
	~SynthWave(void);

	SynthSampInfo* AddSampInfo(void);

	void ConvertTo16bitSigned();

public:
	SynthSampInfo* sampinfo;

	unsigned short wFormatTag;
	unsigned short wChannels;
	ULONG dwSamplesPerSec;
	ULONG dwAveBytesPerSec;
	unsigned short wBlockAlign;
	unsigned short wBitsPerSample;

	unsigned long dataSize;
	unsigned char* data;

	std::string name;
};

