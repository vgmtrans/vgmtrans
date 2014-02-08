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

// *************
// SNES Envelope
// *************

template <class T> void SNESConvADSR(T* rgn, U8 adsr1, U8 adsr2, U8 gain)
{
	bool adsr_enabled = (adsr1 & 0x80) != 0;

	if (adsr_enabled)
	{
		// ADSR mode
		U8 ar = adsr1 & 0x0f;
		U8 dr = (adsr1 & 0x70) >> 4;
		U8 sl = (adsr2 & 0xe0) >> 5;
		U8 sr = adsr2 & 0x1f;

		// TODO: more acculate implementation?
		const double arTable[] = { 4.1, 2.6, 1.5, 1.0, 0.640, 0.380, 0.260, 0.160, 0.096, 0.064, 0.040, 0.024, 0.016, 0.010, 0.006, 0.0 };
		const double drTable[] = { 1.2, 0.740, 0.440, 0.290, 0.180, 0.110, 0.074, 0.037 };
		const double srTable[] = { -1, 38.0, 28.0, 24.0, 19.0, 14.0, 12.0, 9.4, 7.1, 5.9, 4.7, 3.5, 2.9, 2.4, 1.8, 1.5 };
		rgn->attack_time = arTable[ar];
		rgn->decay_time = drTable[dr];
		rgn->sustain_level = sl / 8.0;
		if (sr == 0)
		{
			rgn->sustain_time = -1; // infinite
		}
		else
		{
			rgn->sustain_time = srTable[sr];
		}
	}
	else
	{
		// TODO: GAIN mode
	}
}

// ************
// SNESSampColl
// ************

class SNESSampColl
	: public VGMSampColl
{
public:
	SNESSampColl(const string& format, RawFile* rawfile, U32 offset, UINT maxNumSamps = 256);
	SNESSampColl(const string& format, VGMInstrSet* instrset, U32 offset, UINT maxNumSamps = 256);
	SNESSampColl(const string& format, RawFile* rawfile, U32 offset, const std::vector<BYTE>& targetSRCNs, std::wstring name = L"SNESSampColl");
	SNESSampColl(const string& format, VGMInstrSet* instrset, U32 offset, const std::vector<BYTE>& targetSRCNs, std::wstring name = L"SNESSampColl");
	virtual ~SNESSampColl();

	virtual bool GetSampleInfo();		//retrieve sample info, including pointers to data, # channels, rate, etc.

protected:
	std::vector<BYTE> targetSRCNs;
	U32 spcDirAddr;

	void SetDefaultTargets(UINT maxNumSamps);
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
