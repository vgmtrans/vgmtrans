#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "NDSFormat.h"

class NDSSeq :
	public VGMSeq
{
public:
	NDSSeq(RawFile* file, uint32_t offset, uint32_t length = 0, std::wstring theName = L"NDSSeq");
	//virtual ~NDSSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);

};


class NDSTrack
	: public SeqTrack
{
public:
	NDSTrack(NDSSeq* parentFile, uint32_t offset = 0, uint32_t length = 0);
	void ResetVars();
	virtual bool ReadEvent(void);
	//virtual void SetChannelAndGroupFromTrkNum(int theTrackNum);

	uint8_t jumpCount;
	uint32_t loopReturnOffset;
	bool noteWithDelta;
};