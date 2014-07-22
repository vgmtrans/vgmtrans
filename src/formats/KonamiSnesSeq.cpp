#include "stdafx.h"
#include "KonamiSnesSeq.h"
#include "KonamiSnesFormat.h"
#include "ScaleConversion.h"

using namespace std;

DECLARE_FORMAT(KonamiSnes);

//  **********
//  KonamiSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    48
#define SEQ_KEYOFS  0

KonamiSnesSeq::KonamiSnesSeq(RawFile* file, KonamiSnesVersion ver, uint32_t seqdataOffset, wstring newName)
: VGMSeq(KonamiSnesFormat::name, file, seqdataOffset), version(ver)
{
	name = newName;

	bAllowDiscontinuousTrackData = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap(this);
}

KonamiSnesSeq::~KonamiSnesSeq(void)
{
}

void KonamiSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();

	tempo = 0;
}

bool KonamiSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	// Number of tracks can be less than 8.
	// For instance: Ganbare Goemon 3 - Title
	nNumTracks = MAX_TRACKS;

	VGMHeader* seqHeader = AddHeader(dwOffset, nNumTracks * 2, L"Sequence Header");
	for (uint32_t trackNumber = 0; trackNumber < nNumTracks; trackNumber++)
	{
		uint32_t trackPointerOffset = dwOffset + (trackNumber * 2);
		uint16_t trkOff = GetShort(trackPointerOffset);
		seqHeader->AddPointer(trackPointerOffset, 2, trkOff, true, L"Track Pointer");

		if (trkOff - dwOffset < nNumTracks * 2)
		{
			nNumTracks = (trkOff - dwOffset) / 2;
		}
	}
	seqHeader->unLength = nNumTracks * 2;

	return true;		//successful
}


bool KonamiSnesSeq::GetTrackPointers(void)
{
	for (uint32_t trackNumber = 0; trackNumber < nNumTracks; trackNumber++)
	{
		uint16_t trkOff = GetShort(dwOffset + trackNumber * 2);
		aTracks.push_back(new KonamiSnesTrack(this, trkOff));
	}
	return true;
}

void KonamiSnesSeq::LoadEventMap(KonamiSnesSeq *pSeqFile)
{
}

double KonamiSnesSeq::GetTempoInBPM ()
{
	return GetTempoInBPM(tempo);
}

double KonamiSnesSeq::GetTempoInBPM (uint8_t tempo)
{
	if (tempo != 0)
	{
		return 60000000.0 / (SEQ_PPQN * (125 * 0x20) * 2) * (tempo / 256.0);
	}
	else
	{
		return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
	}
}


//  ************
//  KonamiSnesTrack
//  ************

KonamiSnesTrack::KonamiSnesTrack(KonamiSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

bool KonamiSnesTrack::LoadTrackInit(uint32_t trackNum)
{
	if (!SeqTrack::LoadTrackInit(trackNum))
		return false;

	return true;
}

void KonamiSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	cKeyCorrection = SEQ_KEYOFS;
}

#define EVENT_WITH_MIDITEXT_START	bWriteGenericEventAsTextEventTmp = bWriteGenericEventAsTextEvent; bWriteGenericEventAsTextEvent = true;
#define EVENT_WITH_MIDITEXT_END	bWriteGenericEventAsTextEvent = bWriteGenericEventAsTextEventTmp;

bool KonamiSnesTrack::ReadEvent(void)
{
	KonamiSnesSeq* parentSeq = (KonamiSnesSeq*)this->parentSeq;
	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000)
	{
		return false;
	}

	bool bWriteGenericEventAsTextEventTmp;
	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	wstringstream desc;

	if (statusByte < 0x60)
	{
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		EVENT_WITH_MIDITEXT_START
		AddUnknown(beginOffset, curOffset-beginOffset, L"Note", desc.str().c_str());
		EVENT_WITH_MIDITEXT_END
		bContinue = false;
	}
	else
	{
		KonamiSnesSeqEventType eventType = (KonamiSnesSeqEventType)0;
		map<uint8_t, KonamiSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
		if (pEventType != parentSeq->EventMap.end())
		{
			eventType = pEventType->second;
		}

		switch (eventType)
		{
		case EVENT_UNKNOWN0:
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
			EVENT_WITH_MIDITEXT_START
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_UNKNOWN1:
		{
			uint8_t arg1 = GetByte(curOffset++);
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
				<< std::dec << std::setfill(L' ') << std::setw(0)
				<< L"  Arg1: " << (int)arg1;
			EVENT_WITH_MIDITEXT_START
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_UNKNOWN2:
		{
			uint8_t arg1 = GetByte(curOffset++);
			uint8_t arg2 = GetByte(curOffset++);
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
				<< std::dec << std::setfill(L' ') << std::setw(0)
				<< L"  Arg1: " << (int)arg1
				<< L"  Arg2: " << (int)arg2;
			EVENT_WITH_MIDITEXT_START
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_UNKNOWN3:
		{
			uint8_t arg1 = GetByte(curOffset++);
			uint8_t arg2 = GetByte(curOffset++);
			uint8_t arg3 = GetByte(curOffset++);
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
				<< std::dec << std::setfill(L' ') << std::setw(0)
				<< L"  Arg1: " << (int)arg1
				<< L"  Arg2: " << (int)arg2
				<< L"  Arg3: " << (int)arg3;
			EVENT_WITH_MIDITEXT_START
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_UNKNOWN4:
		{
			uint8_t arg1 = GetByte(curOffset++);
			uint8_t arg2 = GetByte(curOffset++);
			uint8_t arg3 = GetByte(curOffset++);
			uint8_t arg4 = GetByte(curOffset++);
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
				<< std::dec << std::setfill(L' ') << std::setw(0)
				<< L"  Arg1: " << (int)arg1
				<< L"  Arg2: " << (int)arg2
				<< L"  Arg3: " << (int)arg3
				<< L"  Arg4: " << (int)arg4;
			EVENT_WITH_MIDITEXT_START
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
			EVENT_WITH_MIDITEXT_END
			break;
		}

		default:
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
			EVENT_WITH_MIDITEXT_START
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
			EVENT_WITH_MIDITEXT_END
			pRoot->AddLogItem(new LogItem(wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, wstring(L"KonamiSnesSeq")));
			bContinue = false;
			break;
		}
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}

void KonamiSnesTrack::OnTickBegin(void)
{
}

void KonamiSnesTrack::OnTickEnd(void)
{
}
