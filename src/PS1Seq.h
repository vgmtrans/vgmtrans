#pragma once
#include "VGMSeqNoTrks.h"
#include "PS1Format.h"

class PS1Seq :
	public VGMSeqNoTrks
{
public:
	PS1Seq(RawFile* file, ULONG offset);
	virtual ~PS1Seq(void);

	virtual int GetHeaderInfo(void);
	virtual int ReadEvent(void);

protected:
	BYTE runningStatus;
};
