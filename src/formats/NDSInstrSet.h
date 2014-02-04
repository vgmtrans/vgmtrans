#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMColl.h"
#include "NDSFormat.h"

// ***********
// NDSInstrSet
// ***********

class NDSInstrSet :
	public VGMInstrSet
{
public:
	NDSInstrSet(RawFile* file, ULONG offset, ULONG length, wstring name = L"NDS Instrument Bank"/*, VGMSampColl* sampColl = NULL*/);
	virtual bool GetInstrPointers();

	vector<VGMSampColl*> sampCollWAList;
};

// ********
// NDSInstr
// ********


class NDSInstr :
	public VGMInstr
{
public:
	NDSInstr(NDSInstrSet* instrSet, ULONG offset, ULONG length, ULONG bank, ULONG instrNum, BYTE instrType);

	virtual bool LoadInstr();

	void GetSampCollPtr(VGMRgn* rgn, int waNum);
	void GetArticData(VGMRgn* rgn, ULONG offset);
	USHORT GetFallingRate(BYTE DecayTime);
private:
	BYTE instrType;
};



// ***********
// NDSWaveArch
// ***********

//static const unsigned ADPCMTable[89] = 
//{
//	7, 8, 9, 10, 11, 12, 13, 14,
//	16, 17, 19, 21, 23, 25, 28, 31,
//	34, 37, 41, 45, 50, 55, 60, 66,
//	73, 80, 88, 97, 107, 118, 130, 143,
//	157, 173, 190, 209, 230, 253, 279, 307,
//	337, 371, 408, 449, 494, 544, 598, 658,
//	724, 796, 876, 963, 1060, 1166, 1282, 1411,
//	1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
//	3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
//	7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
//	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
//	32767
//};

// I'm using the data from http://nocash.emubase.de/gbatek.htm#dssound
// i should probably reproduce this table programmatically instead
// "The closest way to reproduce the AdpcmTable with 32bit integer maths appears:
//    X=000776d2h, FOR I=0 TO 88, Table[I]=X SHR 16, X=X+(X/10), NEXT I
//    Table[3]=000Ah, Table[4]=000Bh, Table[88]=7FFFh, Table[89..127]=0000h      "

static const unsigned AdpcmTable[89] = 
{
  0x0007,0x0008,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,0x0010,0x0011,0x0013,0x0015,
  0x0017,0x0019,0x001C,0x001F,0x0022,0x0025,0x0029,0x002D,0x0032,0x0037,0x003C,0x0042,
  0x0049,0x0050,0x0058,0x0061,0x006B,0x0076,0x0082,0x008F,0x009D,0x00AD,0x00BE,0x00D1,
  0x00E6,0x00FD,0x0117,0x0133,0x0151,0x0173,0x0198,0x01C1,0x01EE,0x0220,0x0256,0x0292,
  0x02D4,0x031C,0x036C,0x03C3,0x0424,0x048E,0x0502,0x0583,0x0610,0x06AB,0x0756,0x0812,
  0x08E0,0x09C3,0x0ABD,0x0BD0,0x0CFF,0x0E4C,0x0FBA,0x114C,0x1307,0x14EE,0x1706,0x1954,
  0x1BDC,0x1EA5,0x21B6,0x2515,0x28CA,0x2CDF,0x315B,0x364B,0x3BB9,0x41B2,0x4844,0x4F7E,
  0x5771,0x602F,0x69CE,0x7462,0x7FFF
};


//static const int IMA_IndexTable[16] = 
//{
//    -1, -1, -1, -1, 2, 4, 6, 8,
//    -1, -1, -1, -1, 2, 4, 6, 8 
//};

static const int IMA_IndexTable[9] = 
{
    -1, -1, -1, -1, 2, 4, 6, 8
};


class NDSWaveArch :
	public VGMSampColl
{
public:
	NDSWaveArch(RawFile* file, ULONG offset, ULONG length, wstring name = L"NDS Wave Archive");
	virtual ~NDSWaveArch();

	virtual bool GetHeaderInfo();
	virtual bool GetSampleInfo();
};



// *******
// NDSSamp
// *******

class NDSSamp :
	public VGMSamp
{
public:
	NDSSamp(VGMSampColl* sampColl, ULONG offset = 0, ULONG length = 0,
						ULONG dataOffset = 0, ULONG dataLength = 0, BYTE channels = 1, USHORT bps = 16,
						ULONG rate = 0, BYTE waveType = 0, wstring name = L"Sample");

	virtual double GetCompressionRatio(); // ratio of space conserved.  should generally be > 1
								  // used to calculate both uncompressed sample size and loopOff after conversion
	virtual void ConvertToStdWave(BYTE* buf);
	
	void ConvertImaAdpcm(BYTE *buf);

	static inline void clamp_step_index(int& stepIndex);
	static inline void clamp_sample(int& decompSample);
	static inline void process_nibble(unsigned char code, int& stepIndex, int& decompSample);

	enum { PCM8, PCM16, IMA_ADPCM };

	BYTE waveType;
};

