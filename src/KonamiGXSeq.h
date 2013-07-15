#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "KonamiGXFormat.h"

class KonamiGXSeq :
	public VGMSeq
{
public:
	KonamiGXSeq(RawFile* file, ULONG offset);
	virtual ~KonamiGXSeq(void);

	virtual int GetHeaderInfo(void);
	virtual int GetTrackPointers(void);
	//int LoadTracks(void);

protected:

};


class KonamiGXTrack
	: public SeqTrack
{
public:
	KonamiGXTrack(KonamiGXSeq* parentSeq, long offset = 0, long length = 0);

	virtual int ReadEvent(void);

private:
	bool bInJump;
	BYTE prevDelta;
	BYTE prevDur;
	UINT jump_return_offset;
};