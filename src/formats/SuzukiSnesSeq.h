#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SuzukiSnesFormat.h"

extern const uint8_t durtbl[14];

class SuzukiSnesSeq
	: public VGMSeq
{
public:
	SuzukiSnesSeq(RawFile* file, uint32_t seqdata_offset, uint32_t instrtable_offset);
	virtual ~SuzukiSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);

private:
	uint32_t instrtable_offset;
};


class SuzukiSnesTrack
	: public SeqTrack
{
public:
	SuzukiSnesTrack(SuzukiSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual bool ReadEvent(void);

private:
	uint8_t rpt;
	uint8_t rptcnt[10];
	uint32_t rptpos[10];
	uint8_t rptcur[10];
	uint8_t rptoct[10];
};
