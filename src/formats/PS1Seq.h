#pragma once
#include "VGMSeqNoTrks.h"

class PS1Seq :
	public VGMSeqNoTrks
{
public:
	PS1Seq(RawFile* file, uint32_t offset);
	virtual ~PS1Seq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool ReadEvent(void);

protected:
	uint8_t runningStatus;
};
