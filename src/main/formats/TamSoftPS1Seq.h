#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "TamSoftPS1Format.h"

class TamSoftPS1Seq :
	public VGMSeq
{
public:
	TamSoftPS1Seq(RawFile* file, uint32_t offset, uint8_t theSong, const std::wstring & name = L"TamSoftPS1Seq");
	virtual ~TamSoftPS1Seq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);
	virtual void ResetVars(void);

public:
	uint8_t song;
	uint16_t type;
};


class TamSoftPS1Track
	: public SeqTrack
{
public:
	TamSoftPS1Track(TamSoftPS1Seq* parentSeq, uint32_t offset);

	virtual void ResetVars(void);
	virtual bool ReadEvent(void);

protected:
	void FinalizeAllNotes();

	uint32_t lastNoteTime;
	int8_t lastNoteKey;
};
