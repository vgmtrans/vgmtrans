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
		uint16_t unknown1;
		uint16_t unknown2;
		uint16_t trkOffset;
	} TrkInfo;


	TriAcePS1Seq(RawFile* file, ULONG offset);
	virtual ~TriAcePS1Seq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

	VGMHeader* header;
	TrkInfo TrkInfos[32];
	std::vector<TriAcePS1ScorePattern*> aScorePatterns;
	TriAcePS1ScorePattern* curScorePattern;
	std::map<uint32_t, TriAcePS1ScorePattern*> patternMap;
	BYTE initialTempoBPM;
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

	virtual bool LoadTrackMainLoop(ULONG stopOffset);
	uint32_t ReadScorePattern(uint32_t offset);
	virtual bool IsOffsetUsed(ULONG offset);
	virtual void AddEvent(SeqEvent* pSeqEvent);
	virtual bool ReadEvent(void);

	BYTE impliedNoteDur;
	BYTE impliedVelocity;
};