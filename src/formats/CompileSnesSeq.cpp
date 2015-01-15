#include "stdafx.h"
#include "CompileSnesSeq.h"
#include "CompileSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(CompileSnes);

//  **************
//  CompileSnesSeq
//  **************
#define MAX_TRACKS  8
#define SEQ_PPQN    12

#define COMPILESNES_FLAGS_PORTAMENTO    0x10

// duration table
const uint8_t CompileSnesSeq::noteDurTable[] = {
	0x01, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0c, 0x10,
	0x18, 0x20, 0x30, 0x09, 0x12, 0x1e, 0x24, 0x2a,
};

CompileSnesSeq::CompileSnesSeq(RawFile* file, CompileSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
	: VGMSeq(CompileSnesFormat::name, file, seqdataOffset), version(ver),
	STATUS_PERCUSSION_NOTE_MIN(0xc0),
	STATUS_PERCUSSION_NOTE_MAX(0xdd),
	STATUS_DURATION_DIRECT(0xde),
	STATUS_DURATION_MIN(0xdf),
	STATUS_DURATION_MAX(0xee)
{
	name = newName;

	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;
	bWriteInitialTempo = true;

	LoadEventMap(this);
}

CompileSnesSeq::~CompileSnesSeq(void)
{
}

void CompileSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();

	if (aTracks.size() != 0) {
		tempoBPM = GetTempoInBPM(((CompileSnesTrack*)aTracks[0])->spcInitialTempo);
	}
}

bool CompileSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* header = AddHeader(dwOffset, 0);

	header->AddSimpleItem(dwOffset, 1, L"Number of Tracks");
	nNumTracks = GetByte(dwOffset);
	if (nNumTracks == 0 || nNumTracks > 8) {
		return false;
	}

	uint32_t curOffset = dwOffset + 1;
	for (uint8_t trackIndex = 0; trackIndex < nNumTracks; trackIndex++) {
		std::wstringstream trackName;
		trackName << L"Track " << (trackIndex + 1);

		VGMHeader* trackHeader = header->AddHeader(curOffset, 14, trackName.str().c_str());
		trackHeader->AddSimpleItem(curOffset, 1, L"Channel");
		trackHeader->AddSimpleItem(curOffset + 1, 1, L"Flags");
		trackHeader->AddSimpleItem(curOffset + 2, 1, L"Volume");
		trackHeader->AddSimpleItem(curOffset + 3, 1, L"Envelope");
		trackHeader->AddSimpleItem(curOffset + 4, 1, L"Vibrato");
		trackHeader->AddSimpleItem(curOffset + 5, 1, L"Transpose");
		trackHeader->AddTempo(curOffset + 6, 1);
		trackHeader->AddSimpleItem(curOffset + 7, 1, L"Branch ID (Channel #)");
		trackHeader->AddSimpleItem(curOffset + 8, 2, L"Score Pointer");
		trackHeader->AddSimpleItem(curOffset + 10, 1, L"SRCN");
		trackHeader->AddSimpleItem(curOffset + 11, 1, L"ADSR");
		trackHeader->AddSimpleItem(curOffset + 12, 1, L"Pan");
		trackHeader->AddSimpleItem(curOffset + 13, 1, L"Reserved");
		curOffset += 14;
	}

	return true;		//successful
}


bool CompileSnesSeq::GetTrackPointers(void)
{
	uint32_t curOffset = dwOffset + 1;
	for (uint8_t trackIndex = 0; trackIndex < nNumTracks; trackIndex++) {
		uint16_t ofsTrackStart = GetShort(curOffset + 8);

		CompileSnesTrack* track = new CompileSnesTrack(this, ofsTrackStart);
		track->spcInitialFlags = GetByte(curOffset + 1);
		track->spcInitialVolume = GetByte(curOffset + 2);
		track->spcInitialTranspose = (int8_t)GetByte(curOffset + 5);
		track->spcInitialTempo = GetByte(curOffset + 6);
		track->spcInitialPan = (int8_t)GetByte(curOffset + 12);
		aTracks.push_back(track);

		curOffset += 14;
	}

	return true;
}

void CompileSnesSeq::LoadEventMap(CompileSnesSeq *pSeqFile)
{
	for (unsigned int statusByte = 0x00; statusByte <= 0x7f; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_NOTE;
	}

	pSeqFile->EventMap[0x80] = EVENT_GOTO;
	pSeqFile->EventMap[0x81] = EVENT_LOOP_END;
	pSeqFile->EventMap[0x82] = EVENT_END;
	pSeqFile->EventMap[0x83] = EVENT_VIBRATO;
	pSeqFile->EventMap[0x84] = EVENT_PORTAMENTO_TIME;
	//pSeqFile->EventMap[0x85] = 0;
	//pSeqFile->EventMap[0x86] = 0;
	pSeqFile->EventMap[0x87] = EVENT_VOLUME;
	pSeqFile->EventMap[0x88] = EVENT_VOLUME_ENVELOPE;
	pSeqFile->EventMap[0x89] = EVENT_TRANSPOSE;
	pSeqFile->EventMap[0x8a] = EVENT_VOLUME_REL;
	pSeqFile->EventMap[0x8b] = EVENT_UNKNOWN2;
	pSeqFile->EventMap[0x8c] = EVENT_UNKNOWN1; // NOP
	pSeqFile->EventMap[0x8d] = EVENT_LOOP_COUNT;
	pSeqFile->EventMap[0x8e] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0x8f] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0x90] = EVENT_FLAGS;
	pSeqFile->EventMap[0x91] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0x92] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0x93] = EVENT_UNKNOWN2;
	pSeqFile->EventMap[0x94] = EVENT_UNKNOWN1;
	// 95 no version differences
	pSeqFile->EventMap[0x96] = EVENT_TEMPO;
	pSeqFile->EventMap[0x97] = EVENT_TUNING;
	pSeqFile->EventMap[0x98] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0x99] = EVENT_UNKNOWN0;
	pSeqFile->EventMap[0x9a] = EVENT_CALL;
	pSeqFile->EventMap[0x9b] = EVENT_RET;
	//pSeqFile->EventMap[0x9c] = 0;
	pSeqFile->EventMap[0x9d] = EVENT_UNKNOWN1;
	//pSeqFile->EventMap[0x9e] = 0;
	pSeqFile->EventMap[0x9f] = EVENT_ADSR;
	pSeqFile->EventMap[0xa0] = EVENT_PROGCHANGE;
	pSeqFile->EventMap[0xa1] = EVENT_PORTAMENTO_ON;
	pSeqFile->EventMap[0xa2] = EVENT_PORTAMENTO_OFF;
	pSeqFile->EventMap[0xa3] = EVENT_PANPOT_ENVELOPE;
	pSeqFile->EventMap[0xa4] = EVENT_UNKNOWN1; // conditional do (channel match), for delay
	pSeqFile->EventMap[0xa5] = EVENT_UNKNOWN3; // conditional jump
	pSeqFile->EventMap[0xa6] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0xa7] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0xab] = EVENT_PAN;
	pSeqFile->EventMap[0xac] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0xad] = EVENT_LOOP_BREAK;
	pSeqFile->EventMap[0xae] = EVENT_UNKNOWN0;
	pSeqFile->EventMap[0xaf] = EVENT_UNKNOWN0;

	for (unsigned int statusByte = STATUS_PERCUSSION_NOTE_MIN; statusByte <= STATUS_PERCUSSION_NOTE_MAX; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_PERCUSSION_NOTE;
	}

	pSeqFile->EventMap[STATUS_DURATION_DIRECT] = EVENT_DURATION_DIRECT;
	for (unsigned int statusByte = STATUS_DURATION_MIN; statusByte <= STATUS_DURATION_MAX; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_DURATION;
	}

	switch (version) {
	case COMPILESNES_ALESTE:
		pSeqFile->EventMap[0xa4] = EVENT_UNKNOWN1;
		pSeqFile->EventMap[0xa5] = EVENT_UNKNOWN1;
		pSeqFile->EventMap[0xa6] = EVENT_UNKNOWN1;
		break;
	}
}

double CompileSnesSeq::GetTempoInBPM (uint8_t tempo)
{
	unsigned int tempoValue = (tempo == 0) ? 256 : tempo;
	return 60000000.0 / (SEQ_PPQN * (125 * 0x80)) * (tempo / 256.0);
}


//  ****************
//  CompileSnesTrack
//  ****************

CompileSnesTrack::CompileSnesTrack(CompileSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void CompileSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	spcNoteDuration = 1;
	spcFlags = spcInitialFlags;
	spcVolume = spcInitialVolume;
	spcTranspose = spcInitialTranspose;
	spcTempo = spcInitialTempo;
	spcPan = spcInitialPan;
	memset(repeatCount, 0, sizeof(repeatCount));

	transpose = spcTranspose;
}

#define EVENT_WITH_MIDITEXT_START	bWriteGenericEventAsTextEventTmp = bWriteGenericEventAsTextEvent; bWriteGenericEventAsTextEvent = true;
#define EVENT_WITH_MIDITEXT_END	bWriteGenericEventAsTextEvent = bWriteGenericEventAsTextEventTmp;

bool CompileSnesTrack::ReadEvent(void)
{
	CompileSnesSeq* parentSeq = (CompileSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	bool bWriteGenericEventAsTextEventTmp;
	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	CompileSnesSeqEventType eventType = (CompileSnesSeqEventType)0;
	std::map<uint8_t, CompileSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
	if (pEventType != parentSeq->EventMap.end()) {
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

	case EVENT_UNKNOWN5:
	{
		uint8_t arg1 = GetByte(curOffset++);
		uint8_t arg2 = GetByte(curOffset++);
		uint8_t arg3 = GetByte(curOffset++);
		uint8_t arg4 = GetByte(curOffset++);
		uint8_t arg5 = GetByte(curOffset++);
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
			<< std::dec << std::setfill(L' ') << std::setw(0)
			<< L"  Arg1: " << (int)arg1
			<< L"  Arg2: " << (int)arg2
			<< L"  Arg3: " << (int)arg3
			<< L"  Arg4: " << (int)arg4
			<< L"  Arg5: " << (int)arg5;
		EVENT_WITH_MIDITEXT_START
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
		EVENT_WITH_MIDITEXT_END
		break;
	}

	case EVENT_GOTO:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;
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

	case EVENT_LOOP_END:
	{
		uint8_t repeatNest = GetByte(curOffset++);
		uint16_t dest = GetShort(curOffset); curOffset += 2;

		desc << L"Nest Level: " << (int)repeatNest << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Repeat End", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

		repeatCount[repeatNest]--;
		if (repeatCount[repeatNest] != 0) {
			curOffset = dest;
		}
		break;
	}

	case EVENT_END:
	{
		AddEndOfTrack(beginOffset, curOffset-beginOffset);
		bContinue = false;
		break;
	}

	case EVENT_VIBRATO:
	{
		uint8_t envelopeIndex = GetByte(curOffset++);
		desc << L"Envelope Index: " << (int)envelopeIndex;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_PORTAMENTO_TIME:
	{
		uint8_t rate = GetByte(curOffset++);
		desc << L"Rate: " << (int)rate;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Portamento Time", desc.str().c_str(), CLR_PORTAMENTOTIME, ICON_CONTROL);
		break;
	}

	case EVENT_VOLUME:
	{
		uint8_t newVolume = GetByte(curOffset++);
		spcVolume = newVolume;
		uint8_t midiVolume = Convert7bitPercentVolValToStdMidiVal(spcVolume / 2);
		AddVol(beginOffset, curOffset - beginOffset, midiVolume);
		break;
	}

	case EVENT_VOLUME_ENVELOPE:
	{
		uint8_t envelopeIndex = GetByte(curOffset++);
		desc << L"Envelope Index: " << (int)envelopeIndex;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Volume Envelope", desc.str().c_str(), CLR_VOLUME, ICON_CONTROL);
		break;
	}

	case EVENT_TRANSPOSE:
	{
		int8_t delta = (int8_t)GetByte(curOffset++);
		spcTranspose += delta;
		AddTranspose(beginOffset, curOffset - beginOffset, spcTranspose, L"Transpose (Relative)");
		break;
	}

	case EVENT_VOLUME_REL:
	{
		int8_t delta = (int8_t)GetByte(curOffset++);
		spcVolume += delta;
		uint8_t midiVolume = Convert7bitPercentVolValToStdMidiVal(spcVolume / 2);
		AddVol(beginOffset, curOffset - beginOffset, midiVolume, L"Volume (Relative)");
		break;
	}

	case EVENT_NOTE:
	{
		bool rest = (statusByte == 0x00);

		uint8_t duration;
		bool hasDuration = ReadDurationBytes(curOffset, duration);
		if (hasDuration) {
			spcNoteDuration = duration;
			desc << L"Duration: " << (int)duration;
		}

		if (rest) {
			AddRest(beginOffset, curOffset - beginOffset, spcNoteDuration);
		}
		else {
			uint8_t noteNumber = statusByte - 1;
			AddNoteByDur(beginOffset, curOffset - beginOffset, noteNumber, 100, spcNoteDuration,
				hasDuration ? L"Note with Duration" : L"Note");
			AddTime(spcNoteDuration);
		}
		break;
	}

	case EVENT_LOOP_COUNT:
	{
		uint8_t repeatNest = GetByte(curOffset++);
		uint8_t times = GetByte(curOffset++);
		int actualTimes = (times == 0) ? 256 : times;

		desc << L"Nest Level: " << (int)repeatNest << L"  Times: " << actualTimes;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Count", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

		repeatCount[repeatNest] = times;
	}

	case EVENT_FLAGS:
	{
		uint8_t flags = GetByte(curOffset++);
		spcFlags = flags;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Flags", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
	}

	case EVENT_TEMPO:
	{
		uint8_t newTempo = GetByte(curOffset++);
		spcTempo = newTempo;
		AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
		break;
	}

	case EVENT_TUNING:
	{
		int16_t newTuning;
		if (parentSeq->version == COMPILESNES_ALESTE || parentSeq->version == COMPILESNES_JAKICRUSH) {
			newTuning = (int8_t)GetByte(curOffset++);
		}
		else {
			newTuning = (int16_t)GetShort(curOffset); curOffset += 2;
		}

		desc << L"Pitch Register Delta: " << (int)newTuning;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tuning", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_CALL:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern Play", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

		subReturnAddress = curOffset;
		curOffset = dest;
		break;
	}

	case EVENT_RET:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"End Pattern", desc.str().c_str(), CLR_TRACKEND, ICON_ENDREP);
		curOffset = subReturnAddress;
		break;
	}

	case EVENT_PROGCHANGE:
	{
		uint8_t newProg = GetByte(curOffset++);
		AddProgramChange(beginOffset, curOffset - beginOffset, newProg, true);
		break;
	}

	case EVENT_ADSR:
	{
		uint8_t envelopeIndex = GetByte(curOffset++);
		desc << L"Envelope Index: " << (int)envelopeIndex;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", desc.str().c_str(), CLR_VOLUME, ICON_CONTROL);
		break;
	}

	case EVENT_PORTAMENTO_ON:
	{
		spcFlags |= COMPILESNES_FLAGS_PORTAMENTO;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Portamento On", desc.str().c_str(), CLR_PORTAMENTO, ICON_CONTROL);
		break;
	}

	case EVENT_PORTAMENTO_OFF:
	{
		spcFlags &= ~COMPILESNES_FLAGS_PORTAMENTO;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Portamento Off", desc.str().c_str(), CLR_PORTAMENTO, ICON_CONTROL);
		break;
	}

	case EVENT_PANPOT_ENVELOPE:
	{
		uint8_t envelopeIndex = GetByte(curOffset++);
		desc << L"Envelope Index: " << (int)envelopeIndex;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Panpot Envelope", desc.str().c_str(), CLR_PAN, ICON_CONTROL);
		break;
	}

	case EVENT_PAN:
	{
		int8_t newPan = GetByte(curOffset++);
		spcPan = newPan;

		double volumeScale;
		double panpotScale;
		ConvertPercentVolPanToStdMidiScale(volumeScale, panpotScale);
		uint8_t midiPan = Convert7bitPercentPanValToStdMidiVal((uint8_t)(newPan + 0x80) / 2);
		AddPan(beginOffset, curOffset - beginOffset, midiPan);
		break;
	}

	case EVENT_LOOP_BREAK:
	{
		uint8_t repeatNest = GetByte(curOffset++);
		uint16_t dest = GetShort(curOffset); curOffset += 2;

		desc << L"Nest Level: " << (int)repeatNest << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Repeat Break", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

		repeatCount[repeatNest]--;
		if (repeatCount[repeatNest] == 0) {
			curOffset = dest;
		}
		break;
	}

	case EVENT_DURATION_DIRECT:
	{
		uint8_t duration;
		if (!ReadDurationBytes(curOffset, duration)) {
			// address out of range
			return false;
		}
		spcNoteDuration = duration;

		desc << L"Duration: " << (int)duration;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration (Direct)", desc.str().c_str(), CLR_DURNOTE, ICON_CONTROL);
		break;
	}

	case EVENT_DURATION:
	{
		uint8_t duration;
		if (!ReadDurationBytes(curOffset, duration)) {
			// address out of range
			return false;
		}
		spcNoteDuration = duration;

		desc << L"Duration: " << (int)duration;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration", desc.str().c_str(), CLR_DURNOTE, ICON_CONTROL);
		break;
	}

	case EVENT_PERCUSSION_NOTE:
	{
		uint8_t duration;
		bool hasDuration = ReadDurationBytes(curOffset, duration);
		if (hasDuration) {
			spcNoteDuration = duration;
			desc << L"Duration: " << (int)duration;
		}

		uint8_t percNoteNumber = statusByte - parentSeq->STATUS_PERCUSSION_NOTE_MIN;
		AddPercNoteByDur(beginOffset, curOffset - beginOffset, percNoteNumber, 100, spcNoteDuration,
			hasDuration ? L"Percussion Note with Duration" : L"Percussion Note");
		AddTime(spcNoteDuration);
		break;
	}

	default:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		EVENT_WITH_MIDITEXT_START
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
		EVENT_WITH_MIDITEXT_END
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"CompileSnesSeq")));
		bContinue = false;
		break;
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}

bool CompileSnesTrack::ReadDurationBytes(uint32_t& offset, uint8_t& duration)
{
	CompileSnesSeq* parentSeq = (CompileSnesSeq*)this->parentSeq;

	uint32_t curOffset = offset;
	bool durationDispatched = false;
	while (curOffset < 0x10000) {
		uint8_t statusByte = GetByte(curOffset++);

		if (statusByte == parentSeq->STATUS_DURATION_DIRECT) {
			if (curOffset >= 0x10000) {
				break;
			}

			duration = GetByte(curOffset++);
			offset = curOffset;
			durationDispatched = true;
		}
		else if (statusByte >= parentSeq->STATUS_DURATION_MIN && statusByte <= parentSeq->STATUS_DURATION_MAX) {
			duration = CompileSnesSeq::noteDurTable[statusByte - parentSeq->STATUS_DURATION_MIN];
			offset = curOffset;
			durationDispatched = true;
		}
		else {
			break;
		}
	}
	return durationDispatched;
}
