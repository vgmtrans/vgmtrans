#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "Format.h"			//Replace with MP2k-specific format header when that's ready

#define STATE_NOTE 0
#define STATE_TIE 1
#define STATE_TIE_END 2
#define STATE_VOL 3
#define STATE_PAN 4
#define STATE_PITCHBEND 5
#define STATE_MODULATION 6

const uint8_t length_table[0x31] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 
								  0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1C, 0x1E, 0x20, 0x24, 0x28, 0x2A, 0x2C, 0x30, 0x34, 0x36, 0x38, 0x3C, 0x40, 
								  0x42, 0x44, 0x48, 0x4C, 0x4E, 0x50, 0x54, 0x58, 0x5A, 0x5C, 0x60 };


class MP2kSeq :
	public VGMSeq
{
public:
	MP2kSeq(RawFile* file, uint32_t offset, std::wstring name = L"MP2kSeq");
	virtual ~MP2kSeq(void);
	//virtual bool OnSelected(void);
	//virtual bool OnContextMenuCmd(uint32_t nID);
	//virtual bool OnPlay(void);
	//virtual VGMItem* GetItemFromOffset(VGMDoc *pDoc, uint32_t offset);

	//virtual bool Load();								//Function to load all the information about the sequence
	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);	//Function to find all of the track pointers.   Returns number of total tracks.
	//virtual bool Load(uint32_t offset);
	//virtual bool ApplyTable(void);		//create and apply table handler object for sequence

	//void OnSaveAsMidi(void);
};


class MP2kTrack
	: public SeqTrack
{
public:
	MP2kTrack(MP2kSeq* parentSeq, long offset = 0, long length = 0);
	//~FFTTrack(void) {}
	//virtual void SetChannel(int trackNum);
	//virtual void AddEvent(const char* sEventName, int nImage, unsigned long offset, unsigned long length, uint8_t color);
	virtual bool ReadEvent(void);

public:
	uint8_t state;
	uint32_t curDuration;
	uint8_t current_vel;
	//BOOL bInitHold;
	//BOOL bInLoop;
	std::vector<uint32_t> loopEndPositions;
	//BOOL bWasJustInLoop;
};


class MP2kEvent :
	public SeqEvent
{
public:
	MP2kEvent(MP2kTrack* pTrack, uint8_t stateType);
	//virtual bool OnSelected(void);
public:
	uint8_t eventState;					//Keep record of the state, because otherwise, all 0-0x7F events are ambiguous
};