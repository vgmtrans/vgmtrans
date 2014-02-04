#pragma once
#include "VGMSeqNoTrks.h"
#include "SegSatFormat.h"
#include "SegSatScanner.h"

class SegSatSeq :
	public VGMSeqNoTrks
{
public:
	SegSatSeq(RawFile* file, ULONG offset);
	virtual ~SegSatSeq(void);
	//virtual bool OnSelected(void);
	//virtual bool OnContextMenuCmd(UINT nID);
	//virtual bool OnPlay(void);
	//virtual VGMItem* GetItemFromOffset(VGMDoc *pDoc, ULONG offset);

	//virtual bool Load();								//Function to load all the information about the sequence
	virtual bool GetHeaderInfo(void);
	virtual bool ReadEvent(void);
	//virtual bool GetTrackPointers(void);	//Function to find all of the track pointers.   Returns number of total tracks.
	//virtual bool Load(UINT offset);
	//virtual int ApplyTable(void);		//create and apply table handler object for sequence

	//void OnSaveAsMidi(void);

public:
	BYTE headerFlag;
	int remainingEventsInLoop;
	ULONG loopEndPos;
};