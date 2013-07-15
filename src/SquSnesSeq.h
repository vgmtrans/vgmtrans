#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SquSnesFormat.h"

extern const BYTE durtbl[14];

class SquSnesSeq
	: public VGMSeq
{
public:
	SquSnesSeq(RawFile* file, ULONG seqdata_offset, ULONG instrtable_offset);
	virtual ~SquSnesSeq(void);

	virtual int GetHeaderInfo(void);
	virtual int GetTrackPointers(void);

private:
	ULONG instrtable_offset;
};


class SquSnesTrack
	: public SeqTrack
{
public:
	SquSnesTrack(SquSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual int ReadEvent(void);

private:
	BYTE rpt;
	BYTE rptcnt[10];
	ULONG rptpos[10];
	BYTE rptcur[10];
	BYTE rptoct[10];
};
