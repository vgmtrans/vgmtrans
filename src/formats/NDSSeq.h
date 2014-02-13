#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "NDSFormat.h"

class NDSSeq :
	public VGMSeq
{
public:
	NDSSeq(RawFile* file, ULONG offset, ULONG length = 0, std::wstring theName = L"NDSSeq");
	//virtual ~NDSSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);

};


class NDSTrack
	: public SeqTrack
{
public:
	NDSTrack(NDSSeq* parentFile, ULONG offset = 0, ULONG length = 0);
	void ResetVars();
	virtual bool ReadEvent(void);
	//virtual void SetChannelAndGroupFromTrkNum(int theTrackNum);

	BYTE jumpCount;
	ULONG loopReturnOffset;
	bool noteWithDelta;
};