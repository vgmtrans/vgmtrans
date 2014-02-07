#pragma once

#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMItem.h"
#include "ScaleConversion.h"

typedef struct _BRRBlk							//Sample Block
{
	struct
	{
		bool	end:1;							//End block
		bool	loop:1;							//Loop start point
		uint8_t	filter:2;
		uint8_t	range:4;
	} flag;

	uint8_t	brr[8];								//Compressed samples
} BRRBlk;

// ************
// SNESSampColl
// ************

class SNESSampColl
	: public VGMSampColl
{
public:
	SNESSampColl(const string& format, RawFile* rawfile, U32 offset);
	SNESSampColl(const string& format, VGMInstrSet* instrset, U32 offset);
	SNESSampColl(const string& format, RawFile* rawfile, U32 offset, const std::vector<BYTE>& targetSRCNs, std::wstring name = L"SNESSampColl");
	SNESSampColl(const string& format, VGMInstrSet* instrset, U32 offset, const std::vector<BYTE>& targetSRCNs, std::wstring name = L"SNESSampColl");
	virtual ~SNESSampColl();

	virtual bool GetSampleInfo();		//retrieve sample info, including pointers to data, # channels, rate, etc.

protected:
	std::vector<BYTE> targetSRCNs;
	U32 spcDirAddr;

	void SetDefaultTargets();
};

// ********
// SNESSamp
// ********

class SNESSamp
	: public VGMSamp
{
public:
	SNESSamp(VGMSampColl* sampColl, ULONG offset, ULONG length, ULONG dataOffset,
		ULONG dataLen, ULONG loopOffset, std::wstring name = L"BRR");
	virtual ~SNESSamp(void);

	static ULONG GetSampleLength(RawFile * file, ULONG offset);

	virtual double GetCompressionRatio();	// ratio of space conserved.  should generally be > 1
											// used to calculate both uncompressed sample size and loopOff after conversion
	virtual void ConvertToStdWave(BYTE* buf);

private:
	void DecompBRRBlk(int16_t *pSmp, BRRBlk *pVBlk, int32_t *prev1, int32_t *prev2);

private:
	ULONG brrLoopOffset;
};
