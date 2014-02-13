#pragma once

#include "common.h"
#include "RiffFile.h"

struct Loop;
class VGMSamp;

 /*
    Articulation connection graph definitions 
 */
 
 /* Generic Sources */
 #define CONN_SRC_NONE              0x0000
 #define CONN_SRC_LFO               0x0001
 #define CONN_SRC_KEYONVELOCITY     0x0002
 #define CONN_SRC_KEYNUMBER         0x0003
 #define CONN_SRC_EG1               0x0004
 #define CONN_SRC_EG2               0x0005
 #define CONN_SRC_PITCHWHEEL        0x0006
 
 /* Midi Controllers 0-127 */
 #define CONN_SRC_CC1               0x0081
 #define CONN_SRC_CC7               0x0087
 #define CONN_SRC_CC10              0x008a
 #define CONN_SRC_CC11              0x008b
 
 /* Registered Parameter Numbers */
 #define CONN_SRC_RPN0             0x0100
 #define CONN_SRC_RPN1             0x0101
 #define CONN_SRC_RPN2             0x0102
 
 /* Generic Destinations */
 #define CONN_DST_NONE              0x0000
 #define CONN_DST_ATTENUATION       0x0001
 #define CONN_DST_RESERVED          0x0002
 #define CONN_DST_PITCH             0x0003
 #define CONN_DST_PAN               0x0004
 
 /* LFO Destinations */
 #define CONN_DST_LFO_FREQUENCY     0x0104
 #define CONN_DST_LFO_STARTDELAY    0x0105
 
 /* EG1 Destinations */
 #define CONN_DST_EG1_ATTACKTIME    0x0206
 #define CONN_DST_EG1_DECAYTIME     0x0207
 #define CONN_DST_EG1_RESERVED      0x0208
 #define CONN_DST_EG1_RELEASETIME   0x0209
 #define CONN_DST_EG1_SUSTAINLEVEL  0x020a
 
 /* EG2 Destinations */
 #define CONN_DST_EG2_ATTACKTIME    0x030a
 #define CONN_DST_EG2_DECAYTIME     0x030b
 #define CONN_DST_EG2_RESERVED      0x030c
 #define CONN_DST_EG2_RELEASETIME   0x030d
 #define CONN_DST_EG2_SUSTAINLEVEL  0x030e
 
 #define CONN_TRN_NONE              0x0000
 #define CONN_TRN_CONCAVE           0x0001


#define F_INSTRUMENT_DRUMS      0x80000000


#define COLH_SIZE (4+8)
#define INSH_SIZE (12+8)
#define RGNH_SIZE (14+8)//(12+8)
#define WLNK_SIZE (12+8)
#define LIST_HDR_SIZE 12

class DLSInstr;
class DLSRgn;
class DLSArt;
class connectionBlock;
class DLSWsmp;
class DLSWave;

class DLSFile : public RiffFile
{
public:
	DLSFile(std::string dls_name = "Instrument Set");
	~DLSFile(void);

	DLSInstr* AddInstr(unsigned long bank, unsigned long instrNum);
	DLSInstr* AddInstr(unsigned long bank, unsigned long instrNum, std::string Name);
	void DeleteInstr(unsigned long bank, unsigned long instrNum);
	DLSWave* AddWave(USHORT formatTag, USHORT channels, int samplesPerSec, int aveBytesPerSec,
					 USHORT blockAlign, USHORT bitsPerSample, ULONG waveDataSize, BYTE* waveData,
					 std::string name = "Unnamed Wave");
	void SetName(std::string dls_name);

	UINT GetSize(void);

	int WriteDLSToBuffer(std::vector<BYTE> &buf);
	bool SaveDLSFile(const wchar_t* filepath);

public:
	std::vector<DLSInstr*> aInstrs;
	std::vector<DLSWave*> aWaves; 
};

class DLSInstr
{
public:
	DLSInstr(void);
	DLSInstr(ULONG bank, ULONG instrument);
	DLSInstr(ULONG bank, ULONG instrument, std::string instrName);
	DLSInstr(ULONG bank, ULONG instrument, std::string instrName, std::vector<DLSRgn*> listRgns);
	~DLSInstr(void);

	void AddRgnList(std::vector<DLSRgn>& RgnList);
	DLSRgn* AddRgn(void);
	DLSRgn* AddRgn(DLSRgn rgn);

	UINT GetSize(void);
	void Write(std::vector<BYTE> &buf);

public:
	ULONG ulBank;
	ULONG ulInstrument;
 
	std::vector<DLSRgn*> aRgns;
	std::string name;

};

class DLSRgn
{
public:
	DLSRgn(void): Wsmp(NULL), Art(NULL) {}
	DLSRgn(USHORT keyLow, USHORT keyHigh, USHORT velLow, USHORT velHigh)
		: usKeyLow(keyLow), usKeyHigh(keyHigh), usVelLow(velLow), usVelHigh(velHigh), Wsmp(NULL), Art(NULL) {}
	DLSRgn(USHORT keyLow, USHORT keyHigh, USHORT velLow, USHORT velHigh, DLSArt& art);
	//: usKeyLow(keyLow), usKeyHigh(keyHigh), usVelLow(velLow), usVelHigh(velHigh) {}
	~DLSRgn(void);

	DLSArt* AddArt(void);
	DLSArt* AddArt(std::vector<connectionBlock*> connBlocks);
	DLSWsmp* AddWsmp(void);
	DLSWsmp* AddWsmp(DLSWsmp wsmp);
	void SetRanges(USHORT keyLow = 0, USHORT keyHigh = 0x7F, USHORT velLow = 0, USHORT velHigh = 0x7F);
	void SetWaveLinkInfo(USHORT options, USHORT phaseGroup, ULONG theChannel, ULONG theTableIndex);

	UINT GetSize(void);
	void Write(std::vector<BYTE> &buf);

public:
	USHORT usKeyLow;
	USHORT usKeyHigh;
	USHORT usVelLow;
	USHORT usVelHigh;

	USHORT fusOptions;
	USHORT usPhaseGroup;
	ULONG channel;
	ULONG tableIndex;

	DLSWsmp* Wsmp;
	DLSArt* Art;
};

class ConnectionBlock
{
public:
	ConnectionBlock(void);
	ConnectionBlock(USHORT source, USHORT control, USHORT destination, USHORT transform, LONG scale)
		: usSource(source), usControl(control), usDestination(destination), usTransform(transform), lScale(scale) {}
		~ConnectionBlock(void) {}

	UINT GetSize(void) {return 12;}
	void Write(std::vector<BYTE> &buf);

private:
	USHORT usSource;
	USHORT usControl;
	USHORT usDestination;
	USHORT usTransform;
	LONG lScale;
};

class DLSArt
{
public:
	DLSArt(void) {}
	DLSArt(std::vector<ConnectionBlock>& connectionBlocks);
	//DLSArt(USHORT source, USHORT control, USHORT destination, USHORT transform);
	~DLSArt(void);

	void AddADSR(long attack_time, USHORT atk_transform, long decay_time, long sustain_lev, long release_time, USHORT rls_transform);
	void AddPan(long pan);

	UINT GetSize(void);
	void Write(std::vector<BYTE> &buf);

private:
	std::vector<ConnectionBlock*> aConnBlocks;
};

class DLSWsmp
{
public:
	DLSWsmp(void) {}
	DLSWsmp(USHORT unityNote, SHORT fineTune, LONG attenuation, char sampleLoops, ULONG loopType, ULONG loopStart, ULONG loopLength)
		: usUnityNote(unityNote), sFineTune(fineTune), lAttenuation(attenuation), cSampleLoops(sampleLoops), ulLoopType(loopType),
		ulLoopStart(loopStart), ulLoopLength(loopLength) {}
	~DLSWsmp(void) {}

	void SetLoopInfo(Loop& loop, VGMSamp* samp);
	void SetPitchInfo(USHORT unityNote, short fineTune, long attenuation);

	UINT GetSize(void);
	void Write(std::vector<BYTE> &buf);

private:
	unsigned short usUnityNote;
	short sFineTune;
	long lAttenuation;
	char cSampleLoops;

	ULONG ulLoopType;
	ULONG ulLoopStart;
	ULONG ulLoopLength;
};

class DLSWave
{
public:
	DLSWave(void) 
		: Wsmp(NULL), 
		  data(NULL), 
		  name("Untitled Wave") 
	{ 
		RiffFile::AlignName(name); 
	}
	DLSWave(USHORT formatTag, USHORT channels, int samplesPerSec, int aveBytesPerSec, USHORT blockAlign,
		USHORT bitsPerSample, ULONG waveDataSize, unsigned char* waveData, std::string waveName = "Untitled Wave")
		: wFormatTag(formatTag),
		  wChannels(channels),
		  dwSamplesPerSec(samplesPerSec),
		  dwAveBytesPerSec(aveBytesPerSec),
		  wBlockAlign(blockAlign),
		  wBitsPerSample(bitsPerSample),
		  dataSize(waveDataSize),
		  data(waveData),
		  Wsmp(NULL),
		  name(waveName)
	{
		RiffFile::AlignName(name);
	}
	~DLSWave(void);

	//	This function will always return an even value, to maintain the alignment
	// necessary for the RIFF format.
	unsigned long GetSampleSize(void)
	{
		if (dataSize % 2)
			return dataSize+1;
		else
			return dataSize;
//		return dataSize;
	}
	UINT GetSize(void);
	void Write(std::vector<BYTE> &buf);

private:
	DLSWsmp* Wsmp;

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

