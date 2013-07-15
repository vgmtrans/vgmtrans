#pragma once
#include "VGMSeq.h"

class BGMSeq :
	public VGMSeq
{
public:
	BGMSeq(RawFile* file, ULONG offset);
public:
	virtual ~BGMSeq(void);

	virtual int GetHeaderInfo(void);
	virtual int GetTrackPointers(void);	//Function to find all of the track pointers.   Returns number of total tracks.

protected:
	unsigned short seqID;
	unsigned short assocWDID;
};


class BGMTrack
	: public SeqTrack
{
public:
	BGMTrack(BGMSeq* parentSeq, long offset = 0, long length = 0);

	virtual int ReadEvent(void);
};