#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "TriAcePS1Format.h"

class TriAcePS1ScorePattern;

class TriAcePS1Seq :
	public VGMSeq
{
public:
	typedef struct _TrkInfo
	{
		U16 unkown1;
		U16 unknown2;
		U16 trkOffset;
	} TrkInfo;


	TriAcePS1Seq(RawFile* file, ULONG offset);
	virtual ~TriAcePS1Seq(void);

	virtual int GetHeaderInfo(void);
	virtual int GetTrackPointers(void);

	VGMHeader* header;
	TrkInfo TrkInfos[32];
	vector<TriAcePS1ScorePattern*> aScorePatterns;
	TriAcePS1ScorePattern* curScorePattern;
	map<U32, TriAcePS1ScorePattern*> patternMap;
};

class TriAcePS1ScorePattern
	: public VGMContainerItem
{
public:
	TriAcePS1ScorePattern(TriAcePS1Seq* parentSeq, ULONG offset)
	   : VGMContainerItem(parentSeq, offset, 0, L"Score Pattern") { }
};


class TriAcePS1Track
	: public SeqTrack
{
public:
	TriAcePS1Track(TriAcePS1Seq* parentSeq, long offset = 0, long length = 0);

	virtual int LoadTrackMainLoop(U32 stopOffset, long stopDelta);
	U32 ReadScorePattern(U32 offset);
	virtual bool IsOffsetUsed(ULONG offset);
	virtual void AddEvent(SeqEvent* pSeqEvent);
	virtual int ReadEvent(void);

	BYTE impliedNoteDur;
	BYTE impliedVelocity;
};