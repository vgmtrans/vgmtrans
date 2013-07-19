#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "OrgFormat.h"
#include "OrgScanner.h"

class OrgSeq :
	public VGMSeq
{
public:
	OrgSeq(RawFile* file, ULONG offset);
public:
	virtual ~OrgSeq(void);

	virtual int GetHeaderInfo(void);

public:
	USHORT waitTime;		//I believe this is the millis per tick
	BYTE beatsPerMeasure;
};




class OrgTrack
	: public SeqTrack
{
public:
	OrgTrack(OrgSeq* parentFile, long offset, long length, BYTE realTrk);

	virtual int LoadTrack(int trackNum, ULONG stopOffset, long stopDelta);
	virtual int ReadEvent(void);

public:
	BYTE prevPan;

	USHORT curNote;
	BYTE realTrkNum;
	USHORT freq;
	BYTE waveNum;
	USHORT numNotes;
};