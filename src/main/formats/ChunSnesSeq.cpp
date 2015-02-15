#include "stdafx.h"
#include "ChunSnesSeq.h"
#include "ChunSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(ChunSnes);

//  ***********
//  ChunSnesSeq
//  ***********
#define MAX_TRACKS  8
#define SEQ_PPQN    48
#define SEQ_KEYOFS  24

ChunSnesSeq::ChunSnesSeq(RawFile* file, ChunSnesVersion ver, ChunSnesMinorVersion minorVer, uint32_t seqdataOffset, std::wstring newName)
	: VGMSeq(ChunSnesFormat::name, file, seqdataOffset, 0, newName),
	version(ver),
	minorVersion(minorVer)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;
	bUseLinearAmplitudeScale = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap();
}

ChunSnesSeq::~ChunSnesSeq(void)
{
}

void ChunSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();
}

bool ChunSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* header = AddHeader(dwOffset, 0);
	uint32_t curOffset = dwOffset;
	if (curOffset + 2 > 0x10000) {
		return false;
	}

	header->AddTempo(curOffset, 1);
	initialTempo = GetByte(curOffset++);

	bWriteInitialTempo = true;
	tempoBPM = GetTempoInBPM(initialTempo);

	header->AddSimpleItem(curOffset, 1, L"Number of Tracks");
	nNumTracks = GetByte(curOffset++);
	if (nNumTracks == 0 || nNumTracks > MAX_TRACKS) {
		return false;
	}

	for (uint8_t trackIndex = 0; trackIndex < nNumTracks; trackIndex++) {
		uint16_t ofsTrackStart = GetShort(curOffset);

		uint16_t addrTrackStart;
		if (version == CHUNSNES_SUMMER) {
			addrTrackStart = ofsTrackStart;
		}
		else {
			addrTrackStart = dwOffset + ofsTrackStart;
		}

		std::wstringstream trackName;
		trackName << L"Track Pointer " << (trackIndex + 1);
		header->AddSimpleItem(curOffset, 2, trackName.str().c_str());

		ChunSnesTrack* track = new ChunSnesTrack(this, addrTrackStart);
		track->index = aTracks.size();
		aTracks.push_back(track);

		curOffset += 2;
	}

	return true;
}

bool ChunSnesSeq::GetTrackPointers(void)
{
	// already done by GetHeaderInfo
	return true;
}

void ChunSnesSeq::LoadEventMap()
{
	int statusByte;

	for (statusByte = 0x00; statusByte <= 0x9f; statusByte++) {
		EventMap[statusByte] = EVENT_NOTE;
	}
	for (statusByte = 0xa0; statusByte <= 0xdc; statusByte++) {
		EventMap[statusByte] = EVENT_NOP; // DQ5 Bridal March uses it
	}
	EventMap[0xdd] = EVENT_ADSR_RR;
	EventMap[0xde] = EVENT_ADSR_AND_RR;
	EventMap[0xdf] = EVENT_UNKNOWN1;
	EventMap[0xe0] = EVENT_CPU_CONTROLED_JUMP;
	EventMap[0xe1] = EVENT_UNKNOWN0;
	EventMap[0xe2] = EVENT_UNKNOWN1;
	EventMap[0xe3] = EVENT_UNKNOWN1;
	EventMap[0xe4] = EVENT_UNKNOWN1;
	EventMap[0xe5] = EVENT_UNKNOWN2;
	EventMap[0xe6] = EVENT_EXPRESSION_FADE;
	EventMap[0xe7] = EVENT_UNKNOWN1;
	EventMap[0xe8] = EVENT_PAN_FADE;
	EventMap[0xe9] = EVENT_UNKNOWN1;
	EventMap[0xea] = EVENT_GOTO;
	EventMap[0xeb] = EVENT_TEMPO;
	EventMap[0xec] = EVENT_DURATION_RATE;
	EventMap[0xed] = EVENT_VOLUME;
	EventMap[0xee] = EVENT_PAN;
	EventMap[0xef] = EVENT_ADSR;
	EventMap[0xf0] = EVENT_PROGCHANGE;
	EventMap[0xf1] = EVENT_UNKNOWN0;
	EventMap[0xf2] = EVENT_SYNC_NOTE_LEN_ON;
	EventMap[0xf3] = EVENT_SYNC_NOTE_LEN_OFF;
	EventMap[0xf4] = EVENT_LOOP_AGAIN;
	EventMap[0xf5] = EVENT_LOOP_UNTIL;
	EventMap[0xf6] = EVENT_EXPRESSION;
	EventMap[0xf7] = EVENT_UNKNOWN1;
	EventMap[0xf8] = EVENT_CALL;
	EventMap[0xf9] = EVENT_RET;
	EventMap[0xfa] = EVENT_TRANSPOSE;
	EventMap[0xfb] = EVENT_PITCH_SLIDE;
	EventMap[0xfc] = EVENT_UNKNOWN0;
	EventMap[0xfd] = EVENT_UNKNOWN0;
	EventMap[0xfe] = EVENT_UNKNOWN1;
	EventMap[0xff] = EVENT_END;

	if (version != CHUNSNES_SUMMER) {
		for (statusByte = 0xa0; statusByte <= 0xb5; statusByte++) {
			EventMap[statusByte] = EVENT_DURATION_FROM_TABLE;
		}

		// b5-da - not used

		EventMap[0xdb] = EVENT_LOOP_BREAK_ALT;
		EventMap[0xdc] = EVENT_LOOP_AGAIN_ALT;
		EventMap[0xf1] = EVENT_SYNC_NOTE_LEN_ON;
		EventMap[0xf7] = EVENT_NOP;
	}
}

double ChunSnesSeq::GetTempoInBPM(uint8_t tempo)
{
	if (tempo != 0)
	{
		return (double)tempo;
	}
	else
	{
		return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
	}
}

//  *************
//  ChunSnesTrack
//  *************

ChunSnesTrack::ChunSnesTrack(ChunSnesSeq* parentFile, long offset, long length)
	: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void ChunSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	cKeyCorrection = SEQ_KEYOFS;

	vel = 100;
	noteLength = 1;
	noteDurationRate = 0xcc;
	syncNoteLen = false;
	loopCount = 0;
	loopCountAlt = 0;
	subNestLevel = 0;
}


bool ChunSnesTrack::ReadEvent(void)
{
	ChunSnesSeq* parentSeq = (ChunSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	if (syncNoteLen) {
		SyncNoteLengthWithPriorTrack();
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	ChunSnesSeqEventType eventType = (ChunSnesSeqEventType)0;
	std::map<uint8_t, ChunSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
	if (pEventType != parentSeq->EventMap.end()) {
		eventType = pEventType->second;
	}

	switch (eventType)
	{
	case EVENT_UNKNOWN0:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		break;

	case EVENT_UNKNOWN1:
	{
		uint8_t arg1 = GetByte(curOffset++);
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
			<< std::dec << std::setfill(L' ') << std::setw(0)
			<< L"  Arg1: " << (int)arg1;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
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
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
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
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
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
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		break;
	}

	case EVENT_NOP:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"NOP", desc.str().c_str(), CLR_MISC, ICON_BINARY);
		break;
	}

	case EVENT_NOTE: // 00..9f
	{
		uint8_t noteIndex = statusByte;
		if (statusByte >= 0x50) {
			noteLength = GetByte(curOffset++);
			noteIndex -= 0x50;
		}

		bool rest = (noteIndex == 0x00);
		bool slur = (noteIndex == 0x4f);
		uint8_t key = noteIndex - 1;

		// formula for duration is:
		//   dur = len * (durRate + 1) / 256
		// but there are a few of exceptions.
		//   durRate = 0   : full length (tie uses it)
		//   durRate = 254 : full length - 1 (tick)
		uint8_t dur;
		if (noteDurationRate == 254) {
			dur = noteLength - 1;
		}
		else if (noteDurationRate == 0) {
			dur = noteLength;
			slur = true;
		}
		else {
			dur = noteLength * (noteDurationRate + 1) / 256;
		}

		if (rest) {
			AddRest(beginOffset, curOffset - beginOffset, noteLength);
		}
		else if (slur) {
			// TODO: tie
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie/Slur", desc.str().c_str(), CLR_TIE, ICON_NOTE);
			AddTime(noteLength);
		}
		else {
			AddNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur);
			AddTime(noteLength);
		}
	}

	case EVENT_DURATION_FROM_TABLE: // a0..b5
	{
		const uint8_t NOTE_DUR_TABLE[] = {
			0x0d, 0x1a, 0x26, 0x33, 0x40, 0x4d, 0x5a, 0x66,
			0x73, 0x80, 0x8c, 0x99, 0xa6, 0xb3, 0xbf, 0xcc,
			0xd9, 0xe6, 0xf2, 0xfe, 0xff, 0x00
		};

		uint8_t durIndex = statusByte - 0xa0;
		noteDurationRate = NOTE_DUR_TABLE[durIndex];
		if (noteDurationRate == 0) {
			desc << L"Duration Rate: Tie/Slur";
		}
		else if (noteDurationRate == 254) {
			desc << L"Duration Rate: Full - 1";
		}
		else {
			desc << L"Duration Rate: " << ((int)noteDurationRate + 1) << L"/256";
		}
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration Rate from Table", desc.str().c_str(), CLR_DURNOTE);
		break;
	}

	case EVENT_LOOP_BREAK_ALT:
	{
		int16_t destOffset = GetShort(curOffset); curOffset += 2;
		uint16_t dest = curOffset + destOffset;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Break (Alt)", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

		if (loopCountAlt != 0) {
			curOffset = dest;
		}

		break;
	}

	case EVENT_LOOP_AGAIN_ALT:
	{
		int16_t destOffset = GetShort(curOffset); curOffset += 2;
		uint16_t dest = curOffset + destOffset;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Again (Alt)", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

		if (loopCountAlt == 0) {
			loopCountAlt = 2;
		}

		loopCountAlt--;
		if (loopCountAlt != 0) {
			curOffset = dest;
		}

		break;
	}

	case EVENT_ADSR_RR:
	{
		uint8_t release_sr = GetByte(curOffset++) & 15;
		desc << L"SR (Release): " << (int)release_sr;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Release Rate", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_ADSR_AND_RR:
	{
		uint8_t adsr1 = GetByte(curOffset++);
		uint8_t adsr2 = GetByte(curOffset++);
		uint8_t release_sr = GetByte(curOffset++) & 15;

		uint8_t ar = adsr1 & 0x0f;
		uint8_t dr = (adsr1 & 0x70) >> 4;
		uint8_t sl = (adsr2 & 0xe0) >> 5;
		uint8_t sr = adsr2 & 0x1f;

		desc << L"AR: " << (int)ar << L"  DR: " << (int)dr << L"  SL: " << (int)sl << L"  SR: " << (int)sr << L"  SR (Release): " << (int)release_sr;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR & Release Rate", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_CPU_CONTROLED_JUMP:
	{
		int16_t destOffset = GetShort(curOffset); curOffset += 2;
		uint16_t dest = curOffset + destOffset;
		uint8_t condValue = GetByte(curOffset++);
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"CPU-Controled Jump", desc.str().c_str(), CLR_MISC);
		break;
	}

	case EVENT_EXPRESSION_FADE:
	{
		uint8_t vol = GetByte(curOffset++);
		uint8_t fadeLength = GetByte(curOffset++);
		desc << L"Expression: " << (int)vol << L"  Fade Length: " << (int)fadeLength;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Expression Fade", desc.str().c_str(), CLR_EXPRESSION, ICON_CONTROL);
		break;
	}

	case EVENT_PAN_FADE:
	{
		int8_t pan = GetByte(curOffset++);
		uint8_t fadeLength = GetByte(curOffset++);
		desc << L"Pan: " << (int)pan << L"  Fade Length: " << (int)fadeLength;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan Fade", desc.str().c_str(), CLR_PAN, ICON_CONTROL);
		break;
	}

	case EVENT_GOTO:
	{
		int16_t destOffset = GetShort(curOffset); curOffset += 2;
		uint16_t dest = curOffset + destOffset;
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

	case EVENT_TEMPO:
	{
		uint8_t tempoValue = GetByte(curOffset++);

		uint8_t newTempo = tempoValue;
		if (parentSeq->minorVersion == CHUNSNES_WINTER_V3) {
			newTempo = parentSeq->initialTempo * tempoValue / 64;
		}

		AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
		break;
	}

	case EVENT_DURATION_RATE:
	{
		noteDurationRate = GetByte(curOffset++);
		if (noteDurationRate == 0) {
			desc << L"Duration Rate: Tie/Slur";
		}
		else if (noteDurationRate == 254) {
			desc << L"Duration Rate: Full - 1";
		}
		else {
			desc << L"Duration Rate: " << ((int)noteDurationRate + 1) << L"/256";
		}
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration Rate", desc.str().c_str(), CLR_DURNOTE);
		break;
	}

	case EVENT_VOLUME:
	{
		uint8_t vol = GetByte(curOffset++);
		AddVol(beginOffset, curOffset - beginOffset, vol >> 1);
		break;
	}

	case EVENT_PAN:
	{
		int8_t pan = GetByte(curOffset++);
		AddPan(beginOffset, curOffset - beginOffset, (uint8_t)(pan + 0x80) >> 1);
		break;
	}

	case EVENT_ADSR:
	{
		uint8_t adsr1 = GetByte(curOffset++);
		uint8_t adsr2 = GetByte(curOffset++);

		uint8_t ar = adsr1 & 0x0f;
		uint8_t dr = (adsr1 & 0x70) >> 4;
		uint8_t sl = (adsr2 & 0xe0) >> 5;
		uint8_t sr = adsr2 & 0x1f;

		desc << L"AR: " << (int)ar << L"  DR: " << (int)dr << L"  SL: " << (int)sl << L"  SR: " << (int)sr;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_PROGCHANGE:
	{
		uint8_t newProg = GetByte(curOffset++);
		AddProgramChange(beginOffset, curOffset - beginOffset, newProg);
		break;
	}

	case EVENT_SYNC_NOTE_LEN_ON:
	{
		syncNoteLen = true;

		// refresh duration info promptly
		SyncNoteLengthWithPriorTrack();

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Sync Note Length On", desc.str().c_str(), CLR_DURNOTE);
		break;
	}

	case EVENT_SYNC_NOTE_LEN_OFF:
	{
		syncNoteLen = false;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Sync Note Length Off", desc.str().c_str(), CLR_DURNOTE);
		break;
	}

	case EVENT_LOOP_AGAIN:
	{
		int16_t destOffset = GetShort(curOffset); curOffset += 2;
		uint16_t dest = curOffset + destOffset;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Again", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

		if (loopCount == 0) {
			loopCount = 2;
		}

		loopCount--;
		if (loopCount != 0) {
			curOffset = dest;
		}

		break;
	}

	case EVENT_LOOP_UNTIL:
	{
		uint8_t times = GetByte(curOffset++);
		int16_t destOffset = GetShort(curOffset); curOffset += 2;
		uint16_t dest = curOffset + destOffset;
		desc << L"Times: " << (int)times << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Until", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

		if (loopCount == 0) {
			loopCount = times;
		}

		loopCount--;
		if (loopCount != 0) {
			curOffset = dest;
		}

		break;
	}

	case EVENT_EXPRESSION:
	{
		uint8_t vol = GetByte(curOffset++);
		AddExpression(beginOffset, curOffset - beginOffset, vol >> 1);
		break;
	}

	case EVENT_CALL:
	{
		int16_t destOffset = GetShort(curOffset); curOffset += 2;
		uint16_t dest = curOffset + destOffset;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern Play", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

		if (subNestLevel >= CHUNSNES_SUBLEVEL_MAX) {
			// stack overflow
			bContinue = false;
			break;
		}

		subReturnAddr[subNestLevel] = curOffset;
		subNestLevel++;

		curOffset = dest;
		break;
	}

	case EVENT_RET:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern End", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

		if (subNestLevel > 0) {
			curOffset = subReturnAddr[subNestLevel - 1];
			subNestLevel--;
		}

		break;
	}

	case EVENT_TRANSPOSE:
	{
		int8_t newTranspose = GetByte(curOffset++);
		AddTranspose(beginOffset, curOffset - beginOffset, newTranspose);
		break;
	}

	case EVENT_PITCH_SLIDE:
	{
		int8_t semitones = GetByte(curOffset++);
		uint8_t length = GetByte(curOffset++);
		desc << L"Key: " << (semitones > 0 ? L"+" : L"") << (int)semitones << L" semitones" << L"  Length: " << (int)length;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_END:
	{
		AddEndOfTrack(beginOffset, curOffset - beginOffset);
		bContinue = false;
	}

	default:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"ChunSnesSeq")));
		bContinue = false;
		break;
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}

void ChunSnesTrack::SyncNoteLengthWithPriorTrack()
{
	if (index != 0 && index < parentSeq->aTracks.size()) {
		ChunSnesTrack* priorTrack = (ChunSnesTrack*)parentSeq->aTracks[index - 1];
		noteLength = priorTrack->noteLength;
		noteDurationRate = priorTrack->noteDurationRate;
	}
}
