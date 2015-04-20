#include "pch.h"
#include "ScaleConversion.h"
#include "TamSoftPS1Seq.h"
#include "TamSoftPS1Format.h"

DECLARE_FORMAT(TamSoftPS1);

//  *************
//  TamSoftPS1Seq
//  *************
#define TSQ_PPQN            24
#define TSQ_MAX_TRACKS      24
#define TSQ_SONG_TABLE_SIZE (4 * TAMSOFTPS1_MAX_SONGS)
#define TSQ_HEADER_SIZE     (4 * TSQ_MAX_TRACKS)

TamSoftPS1Seq::TamSoftPS1Seq(RawFile* file, uint32_t offset, uint8_t theSong, const std::wstring & name)
	: VGMSeq(TamSoftPS1Format::name, file, offset, 0, name), song(theSong), type(0)
{
	bLoadTickByTick = true;
	bUseLinearAmplitudeScale = true;

	const double PSX_NTSC_FRAMERATE = 53222400.0 / 263.0 / 3413.0;
	AlwaysWriteInitialTempo(60.0 / (TSQ_PPQN / PSX_NTSC_FRAMERATE));

	UseReverb();
	AlwaysWriteInitialReverb(0);
}

TamSoftPS1Seq::~TamSoftPS1Seq(void)
{
}

void TamSoftPS1Seq::ResetVars(void)
{
	VGMSeq::ResetVars();

	// default reverb depth depends on each games, probably
	reverbDepth = 0x4000;
}

bool TamSoftPS1Seq::GetHeaderInfo(void)
{
	SetPPQN(TSQ_PPQN);

	if (dwOffset + TSQ_SONG_TABLE_SIZE > vgmfile->GetEndOffset()) {
		return false;
	}

	uint32_t dwSongItemOffset = dwOffset + 4 * song;
	type = GetShort(dwSongItemOffset);
	uint16_t seqHeaderRelOffset = GetShort(dwSongItemOffset + 2);

	std::wstringstream songTableItemName;
	songTableItemName << L"Song " << song;
	VGMHeader * songTableItem = AddHeader(dwSongItemOffset, 4, songTableItemName.str());
	songTableItem->AddSimpleItem(dwSongItemOffset, 2, L"BGM/SFX");
	songTableItem->AddSimpleItem(dwSongItemOffset + 2, 2, L"Header Offset");

	if (seqHeaderRelOffset < TSQ_SONG_TABLE_SIZE) {
		return false;
	}

	if (type == 0) {
		// BGM
		uint32_t dwHeaderOffset = dwOffset + seqHeaderRelOffset;

		// ignore (corrupted) silence sequence
		if (GetWord(dwHeaderOffset) != 0xfffff0) {
			VGMHeader * seqHeader = AddHeader(dwHeaderOffset, TSQ_HEADER_SIZE);
			for (uint8_t trackIndex = 0; trackIndex < TSQ_MAX_TRACKS; trackIndex++) {
				uint32_t dwTrackHeaderOffset = dwHeaderOffset + 4 * trackIndex;

				std::wstringstream trackHeaderName;
				trackHeaderName << L"Track " << (trackIndex + 1);
				VGMHeader * trackHeader = seqHeader->AddHeader(dwTrackHeaderOffset, 4, trackHeaderName.str());

				uint8_t live = GetByte(dwTrackHeaderOffset);
				uint32_t dwRelTrackOffset = GetShort(dwTrackHeaderOffset + 2);
				trackHeader->AddSimpleItem(dwTrackHeaderOffset, 1, L"Active/Inactive");
				trackHeader->AddSimpleItem(dwTrackHeaderOffset + 1, 1, L"Padding");
				trackHeader->AddSimpleItem(dwTrackHeaderOffset + 2, 2, L"Track Offset");

				if (live != 0) {
					if (dwHeaderOffset + dwRelTrackOffset < vgmfile->GetEndOffset()) {
						TamSoftPS1Track * track = new TamSoftPS1Track(this, dwHeaderOffset + dwRelTrackOffset);
						aTracks.push_back(track);
					}
					else {
						return false;
					}
				}
			}
		}
	}
	else {
		// SFX (single track)
		uint32_t dwTrackOffset = dwOffset + seqHeaderRelOffset;

		// ignore silence sequence
		if (GetShort(dwTrackOffset) != 0xfff0) {
			TamSoftPS1Track * track = new TamSoftPS1Track(this, dwTrackOffset);
			aTracks.push_back(track);
		}
	}

	return true;
}

bool TamSoftPS1Seq::GetTrackPointers(void)
{
	return true;
}

//  ***************
//  TamSoftPS1Track
//  ***************

TamSoftPS1Track::TamSoftPS1Track(TamSoftPS1Seq* parentFile, uint32_t offset)
	: SeqTrack(parentFile, offset)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	//bWriteGenericEventAsTextEvent = true;
}

void TamSoftPS1Track::ResetVars(void)
{
	SeqTrack::ResetVars();

	vel = 100;
	lastNoteKey = -1;
}

bool TamSoftPS1Track::ReadEvent(void)
{
	TamSoftPS1Seq* parentSeq = (TamSoftPS1Seq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= vgmfile->GetEndOffset()) {
		FinalizeAllNotes();
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	if (statusByte >= 0x00 && statusByte <= 0x7f) {
		desc << L"Delta Time: " << statusByte;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Delta Time", desc.str(), CLR_REST);
		AddTime(statusByte);
	}
	else if (statusByte >= 0x80 && statusByte <= 0xdf) {
		uint8_t key = statusByte & 0x7f;
		desc << L"Key: " << key;

		if (lastNoteKey >= 0) {
			FinalizeAllNotes();
		}
		lastNoteKey = key;
		lastNoteTime = GetTime();

		AddNoteOn(beginOffset, curOffset - beginOffset, TAMSOFTPS1_KEY_OFFSET + key, vel);
	}
	else {
		switch (statusByte)
		{
		case 0xE0:
		{
			uint8_t vol = GetByte(curOffset++) / 2;
			AddVol(beginOffset, curOffset - beginOffset, vol);
			break;
		}

		case 0xE1:
		{
			uint8_t volumeBalanceLeft = GetByte(curOffset++);
			uint8_t volumeBalanceRight = GetByte(curOffset++);

			double volumeScale;
			uint8_t midiPan = ConvertVolumeBalanceToStdMidiPan(volumeBalanceLeft / 256.0, volumeBalanceRight / 256.0, &volumeScale);

			desc << L"Left Volume: " << volumeBalanceLeft << L"  Right Volume: " << volumeBalanceRight;
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Volume Balance", desc.str(), CLR_PAN, ICON_CONTROL);
			AddPanNoItem(midiPan);
			break;
		}

		case 0xE2:
		{
			uint8_t progNum = GetByte(curOffset++);
			AddProgramChange(beginOffset, curOffset - beginOffset, progNum, true);
			break;
		}

		case 0xE3:
		{
			uint16_t a1 = GetShort(curOffset); curOffset += 2;
			AddUnknown(beginOffset, curOffset - beginOffset, L"NOP", desc.str());
			break;
		}

		case 0xE4:
		{
			int16_t pitch = GetShort(curOffset); curOffset += 2;
			desc << L"Pitch: " << pitch;
			AddUnknown(beginOffset, curOffset - beginOffset, L"Pitch Bend?", desc.str());
			break;
		}

		case 0xE5:
		{
			int16_t pitch = GetShort(curOffset); curOffset += 2;
			desc << L"Pitch: " << pitch;
			AddUnknown(beginOffset, curOffset - beginOffset, L"Pitch Bend?", desc.str());
			break;
		}

		case 0xE6:
		{
			uint8_t mode = GetByte(curOffset++);
			desc << L"Reverb Mode: " << mode;
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reverb Mode", desc.str(), CLR_REVERB, ICON_CONTROL);
			break;
		}

		case 0xE7:
		{
			uint8_t depth = GetByte(curOffset++);
			desc << L"Reverb Depth: " << depth;
			parentSeq->reverbDepth = depth << 8;
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reverb Depth", desc.str(), CLR_REVERB, ICON_CONTROL);
			break;
		}

		case 0xE8:
		{
			uint8_t midiReverb = roundi(fabs(parentSeq->reverbDepth / 32768.0) * 127.0);
			AddReverb(beginOffset, curOffset - beginOffset, midiReverb, L"Reverb On");
			break;
		}

		case 0xE9:
		{
			AddReverb(beginOffset, curOffset - beginOffset, 0, L"Reverb Off");
			break;
		}

		case 0xEA:
		{
			uint16_t pitchScale = GetShort(curOffset); curOffset += 2;
			double cents = PitchScaleToCents(pitchScale / 4096.0);
			AddFineTuning(beginOffset, curOffset - beginOffset, cents);
			break;
		}

		case 0xF0:
		{
			FinalizeAllNotes();
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Note Off", desc.str(), CLR_NOTEOFF, ICON_NOTE);
			break;
		}

		case 0xF1:
		{
			uint16_t a1 = GetByte(curOffset++);
			desc << L"Arg1: " << a1;
			AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event F1", desc.str());
			break;
		}

		case 0xF8:
		{
			int16_t relOffset = GetShort(curOffset); curOffset += 2;

			uint32_t dest = curOffset + relOffset;
			desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
			uint32_t length = curOffset - beginOffset;

			curOffset = dest;
			if (!IsOffsetUsed(dest)) {
				AddGenericEvent(beginOffset, length, L"Jump", desc.str().c_str(), CLR_LOOPFOREVER);
			}
			else {
				bContinue = AddLoopForever(beginOffset, length, L"Jump");
			}
			break;
		}

		case 0xF9:
		{
			AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event F9", desc.str());
			break;
		}

		case 0xFF:
			// I'm quite not sure, but it looks like an end event
			bContinue = AddEndOfTrack(beginOffset, curOffset - beginOffset);
			break;

		default:
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
			AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str());
			pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"TamSoftPS1Seq")));
			bContinue = false;
			break;
		}
	}

	//std::wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	if (!bContinue) {
		FinalizeAllNotes();
	}

	return bContinue;
}

void TamSoftPS1Track::FinalizeAllNotes()
{
	if (lastNoteTime != GetTime()) {
		AddNoteOffNoItem(TAMSOFTPS1_KEY_OFFSET + lastNoteKey);
	}
	else {
		// zero length note (Choro Q Wonderful! DEMO.TSQ)
		// convert it to length=1 for safe
		InsertNoteOffNoItem(TAMSOFTPS1_KEY_OFFSET + lastNoteKey, lastNoteTime + 1);
	}
	lastNoteKey = -1;
}
