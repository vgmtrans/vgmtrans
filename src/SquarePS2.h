#pragma once
#include "VGMSeq.h"

class BGMSeq :
	public VGMSeq
{
public:
	BGMSeq(RawFile* file, ULONG offset);
public:
	virtual ~BGMSeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);	//Function to find all of the track pointers.   Returns number of total tracks.

protected:
	unsigned short seqID;
	unsigned short assocWDID;
};


class BGMTrack
	: public SeqTrack
{
public:
	BGMTrack(BGMSeq* parentSeq, long offset = 0, long length = 0);

	virtual bool ReadEvent(void);
};