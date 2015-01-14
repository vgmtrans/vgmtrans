#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "AkaoSnesFormat.h"

extern const uint8_t durtbl[14];

class AkaoSnesSeq
	: public VGMSeq
{
public:
	AkaoSnesSeq(RawFile* file, uint32_t seqdata_offset, uint32_t instrtable_offset);
	virtual ~AkaoSnesSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);

private:
	uint32_t instrtable_offset;
};


class AkaoSnesTrack
	: public SeqTrack
{
public:
	AkaoSnesTrack(AkaoSnesSeq* parentFile, long offset = 0, long length = 0);
	virtual bool ReadEvent(void);

private:
	uint8_t rpt;
	uint8_t rptcnt[10];
	uint32_t rptpos[10];
	uint8_t rptcur[10];
	uint8_t rptoct[10];
};
