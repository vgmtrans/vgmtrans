#pragma once
#include "VGMSeqNoTrks.h"
#include "SeqTrack.h"
#include "SonyPS2Format.h"

#define COMP_KEYON2POLY		1		//compression mode

class SonyPS2Seq :
	public VGMSeqNoTrks
{
public:
	typedef struct _HdrCk
	{
		U32 Creator;
		U32 Type;
		U32 chunkSize;
		U32 fileSize;
		U32	songChunkAddr;
		U32 midiChunkAddr;
		U32 seSequenceChunkAddr;
		U32 seSongChunkAddr;
	} HdrCk;

	SonyPS2Seq(RawFile* file, ULONG offset);
	virtual ~SonyPS2Seq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool ReadEvent(void);
	BYTE GetDataByte(U32 offset);

protected:
	VersCk versCk;
	HdrCk hdrCk;
	U32 midiChunkSize;
	U32 maxMidiNumber;	//this determines the number of midi blocks.  Seems very similar to SMF Type 2 (blech!)
						//for the moment, I'm going to assume the maxMidiNumber is always 0 (meaning one midi data block)
	U32 midiOffsetAddr;	//the offset for midi data block 0.  I won't bother with > 0 data blocks for the time being
	U16 compOption;		//determines compression mode for midi data block 0

	bool bSkipDeltaTime;
	BYTE runningStatus;
};

