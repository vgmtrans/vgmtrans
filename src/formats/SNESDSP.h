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

class SNESSamp
	: public VGMSamp
{
public:
	SNESSamp(VGMSampColl* sampColl, ULONG offset, ULONG length, ULONG dataOffset,
		ULONG dataLen, ULONG loopOffset, wstring name);
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
