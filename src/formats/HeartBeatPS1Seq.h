#pragma once
#include "VGMSeqNoTrks.h"

class HeartBeatPS1Seq :
	public VGMSeqNoTrks
{
public:
	HeartBeatPS1Seq(RawFile* file, uint32_t offset);
	virtual ~HeartBeatPS1Seq(void);

	virtual bool GetHeaderInfo(void);
	virtual void ResetVars();
	virtual bool ReadEvent(void);

protected:
	uint8_t runningStatus;
};
