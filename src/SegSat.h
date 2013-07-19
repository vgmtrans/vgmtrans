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
	//virtual int OnSelected(void);
	//virtual int OnContextMenuCmd(UINT nID);
	//virtual int OnPlay(void);
	//virtual VGMItem* GetItemFromOffset(VGMDoc *pDoc, ULONG offset);

	//virtual int Load();								//Function to load all the information about the sequence
	virtual int GetHeaderInfo(void);
	virtual int ReadEvent(void);
	//virtual int GetTrackPointers(void);	//Function to find all of the track pointers.   Returns number of total tracks.
	//virtual int Load(UINT offset);
	//virtual int ApplyTable(void);		//create and apply table handler object for sequence

	//void OnSaveAsMidi(void);

public:
	BYTE headerFlag;
	int remainingEventsInLoop;
	ULONG loopEndPos;
};