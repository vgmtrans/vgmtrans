#include "stdafx.h"
#include "NinSnesSeq.h"
#include "NinSnesFormat.h"
#include "Options.h"

DECLARE_FORMAT(NinSnes);

//  **********
//  NinSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    48
#define SEQ_KEYOFS  24

NinSnesSeq::NinSnesSeq(RawFile* file, NinSnesVersion ver, uint32_t offset, std::wstring theName)
	: VGMMultiSectionSeq(NinSnesFormat::name, file, offset, 0), version(ver),
	header(NULL),
	volumeTable(NULL),
	durRateTable(NULL),
	panTable(NULL)
{
	name = theName;
}

NinSnesSeq::~NinSnesSeq()
{
	if (volumeTable != NULL) {
		delete [] volumeTable;
	}

	if (durRateTable != NULL) {
		delete [] durRateTable;
	}
	
	if (panTable != NULL) {
		delete [] panTable;
	}
}

void NinSnesSeq::ResetVars()
{
	VGMMultiSectionSeq::ResetVars();

	sectionRepeatCount = 0;
}

bool NinSnesSeq::GetHeaderInfo()
{
	SetPPQN(SEQ_PPQN);
	nNumTracks = MAX_TRACKS;

	// events will be added later, see ReadEvent
	header = AddHeader(dwStartOffset, 0);
	return true;
}

bool NinSnesSeq::ReadEvent()
{
	uint32_t beginOffset = curOffset;
	if (curOffset + 1 >= 0x10000) {
		return false;
	}

	uint16_t sectionAddress = GetShort(curOffset); curOffset += 2;
	bool bContinue = true;

	if (sectionAddress == 0) {
		// End
		header->AddSimpleItem(beginOffset, curOffset - beginOffset, L"Section Playlist End");
		bContinue = false;
	}
	else if (sectionAddress <= 0xff) {
		// Jump
		if (curOffset + 1 >= 0x10000) {
			return false;
		}

		uint16_t repeatCount = sectionAddress;
		uint16_t dest = GetShort(curOffset); curOffset += 2;

		bool startNewRepeat = false;
		bool doJump = false;
		bool infiniteLoop = false;

		// decrease 8-bit counter
		sectionRepeatCount--;
		// check overflow (end of loop)
		if (sectionRepeatCount >= 0x80) {
			// it's over, set new repeat count
			sectionRepeatCount = (uint8_t)repeatCount;
			startNewRepeat = true;
		}

		// jump if the counter is zero
		if (sectionRepeatCount != 0) {
			doJump = true;
			if (IsOffsetUsed(dest) && sectionRepeatCount > 0x80) {
				infiniteLoop = true;
			}
		}

		// add event to sequence
		if (infiniteLoop) {
			header->AddSimpleItem(beginOffset, curOffset - beginOffset, L"Playlist Jump");
			foreverLoops++;

			if (readMode == READMODE_ADD_TO_UI)
			{
				bContinue = false;
			}
			else if (readMode == READMODE_FIND_DELTA_LENGTH)
			{
				bContinue = (foreverLoops < ConversionOptions::GetNumSequenceLoops());
			}
		}
		else {
			header->AddSimpleItem(beginOffset, curOffset - beginOffset, L"Playlist Jump");
		}

		// add the last event too, if available
		if (curOffset + 1 < 0x10000 && GetShort(curOffset) == 0x0000) {
			header->AddSimpleItem(curOffset, 2, L"Playlist End");
		}

		// do actual jump, at last
		if (doJump) {
			curOffset = dest;
		}
	}
	else {
		// Play the section
		header->AddSimpleItem(beginOffset, curOffset - beginOffset, L"Section Pointer");

		NinSnesSection* section = (NinSnesSection*)GetSectionFromOffset(sectionAddress);
		if (section == NULL) {
			section = new NinSnesSection(this, sectionAddress);
			if (!section->Load()) {
				pRoot->AddLogItem(new LogItem(L"Failed to load section\n", LOG_LEVEL_ERR, L"NinSnesSeq"));
				return false;
			}
			AddSection(section);
		}

		// TODO: read track events in section
	}

	return bContinue;
}

//  **************
//  NinSnesSection
//  **************

NinSnesSection::NinSnesSection(NinSnesSeq* parentFile, long offset, long length)
	: VGMSeqSection(parentFile, offset, length)
{
}

bool NinSnesSection::GetTrackPointers()
{
	uint32_t curOffset = dwOffset;

	VGMHeader* header = AddHeader(curOffset, 16);
	for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		if (curOffset + 1 >= 0x10000) {
			return false;
		}

		uint16_t startAddress = GetShort(curOffset);

		bool active = ((startAddress & 0xff00) != 0);
		if (active) {
			aTracks.push_back(new NinSnesTrack(this, startAddress));
		}
		else {
			// add an inactive track
			NinSnesTrack* track = new NinSnesTrack(this, curOffset, 0, L"NULL");
			track->available = false;
			aTracks.push_back(track);
		}

		wchar_t name[32];
		wsprintf(name, L"Track Pointer #%d", trackIndex + 1);

		header->AddSimpleItem(curOffset, 2, name);
		curOffset += 2;
	}

	return true;
}

//  ************
//  NinSnesTrack
//  ************

NinSnesTrack::NinSnesTrack(NinSnesSection* parentSection, long offset, long length, const wchar_t* theName)
	: SeqTrack(parentSection->parentSeq, offset, length),
	parentSection(parentSection),
	available(true)
{
	if (theName != NULL) {
		name = theName;
	}

	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
}

bool NinSnesTrack::ReadEvent(void)
{
	NinSnesSeq* parentSeq = (NinSnesSeq*)this->parentSeq;
	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000)
	{
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	// TODO: NinSnesTrack::ReadEvent
	bContinue = false;

	return bContinue;
}
