#include "stdafx.h"
#include "PrismSnesSeq.h"
#include "PrismSnesFormat.h"
#include "ScaleConversion.h"

// TODO: Fix envelope event length

DECLARE_FORMAT(PrismSnes);

//  ************
//  PrismSnesSeq
//  ************
#define MAX_TRACKS  24
#define SEQ_PPQN    48

const uint8_t PrismSnesSeq::PAN_TABLE_1[21] = {
	0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
	0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
	0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
};

const uint8_t PrismSnesSeq::PAN_TABLE_2[21] = {
	0x1e, 0x28, 0x32, 0x3c, 0x46, 0x50, 0x5a, 0x64,
	0x6e, 0x78, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
	0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
};

PrismSnesSeq::PrismSnesSeq(RawFile* file, PrismSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
	: VGMSeq(PrismSnesFormat::name, file, seqdataOffset, 0, newName), version(ver),
	envContainer(NULL)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;
	bUseLinearAmplitudeScale = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);
	AlwaysWriteInitialTempo(GetTempoInBPM(0x82));

	LoadEventMap();
}

PrismSnesSeq::~PrismSnesSeq(void)
{
}

void PrismSnesSeq::DemandEnvelopeContainer(uint32_t offset)
{
	if (envContainer == NULL) {
		envContainer = AddHeader(offset, 0, L"Envelopes");
	}

	if (offset < envContainer->dwOffset) {
		if (envContainer->unLength != 0) {
			envContainer->unLength += envContainer->dwOffset - offset;
		}
		envContainer->dwOffset = offset;
	}
}

void PrismSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();

	conditionSwitch = false;
}

bool PrismSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* header = AddHeader(dwOffset, 0);

	uint32_t curOffset = dwOffset;
	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS + 1; trackIndex++) {
		if (curOffset + 1 > 0x10000) {
			return false;
		}

		uint8_t channel = GetByte(curOffset);
		if (channel >= 0x80) {
			header->AddSimpleItem(curOffset, 1, L"Header End");
			break;
		}
		if (trackIndex >= MAX_TRACKS) {
			return false;
		}

		std::wstringstream trackName;
		trackName << L"Track " << (trackIndex + 1);
		VGMHeader* trackHeader = header->AddHeader(curOffset, 4, trackName.str());

		trackHeader->AddSimpleItem(curOffset, 1, L"Logical Channel");
		curOffset++;

		uint8_t a01 = GetByte(curOffset);
		trackHeader->AddSimpleItem(curOffset, 1, L"Physical Channel + Flags");
		curOffset++;

		uint16_t addrTrackStart = GetShort(curOffset);
		trackHeader->AddSimpleItem(curOffset, 2, L"Track Pointer");
		curOffset += 2;

		PrismSnesTrack* track = new PrismSnesTrack(this, addrTrackStart);
		aTracks.push_back(track);
	}

	return true;
}

bool PrismSnesSeq::GetTrackPointers(void)
{
	return true;
}

void PrismSnesSeq::LoadEventMap()
{
	int statusByte;

	for (statusByte = 0x00; statusByte <= 0x7f; statusByte++) {
		EventMap[statusByte] = EVENT_NOTE;
	}

	for (statusByte = 0x80; statusByte <= 0x9f; statusByte++) {
		EventMap[statusByte] = EVENT_NOISE_NOTE;
	}

	EventMap[0xc0] = EVENT_TEMPO;
	EventMap[0xc1] = EVENT_TEMPO;
	EventMap[0xc2] = EVENT_TEMPO;
	EventMap[0xc3] = EVENT_TEMPO;
	EventMap[0xc4] = EVENT_TEMPO;
	EventMap[0xc5] = EVENT_CONDITIONAL_JUMP;
	EventMap[0xc6] = EVENT_CONDITION;
	EventMap[0xc7] = EVENT_UNKNOWN1;
	EventMap[0xc8] = EVENT_UNKNOWN2;
	EventMap[0xc9] = EVENT_UNKNOWN2;
	EventMap[0xca] = EVENT_RESTORE_ECHO_PARAM;
	EventMap[0xcb] = EVENT_SAVE_ECHO_PARAM;
	EventMap[0xcc] = EVENT_UNKNOWN1;
	EventMap[0xcd] = EVENT_SLUR_OFF;
	EventMap[0xce] = EVENT_SLUR_ON;
	EventMap[0xcf] = EVENT_VOLUME_ENVELOPE;
	EventMap[0xd0] = EVENT_DEFAULT_PAN_TABLE_1;
	EventMap[0xd1] = EVENT_DEFAULT_PAN_TABLE_2;
	EventMap[0xd2] = EVENT_UNKNOWN0;
	EventMap[0xd3] = EVENT_INC_APU_PORT_3;
	EventMap[0xd4] = EVENT_INC_APU_PORT_2;
	EventMap[0xd5] = EVENT_PLAY_SONG_3;
	EventMap[0xd6] = EVENT_PLAY_SONG_2;
	EventMap[0xd7] = EVENT_PLAY_SONG_1;
	EventMap[0xd8] = EVENT_TRANSPOSE_REL;
	EventMap[0xd9] = EVENT_PAN_ENVELOPE;
	EventMap[0xda] = EVENT_PAN_TABLE;
	EventMap[0xdb] = EVENT_NOP2;
	EventMap[0xdc] = EVENT_DEFAULT_LENGTH_OFF;
	EventMap[0xdd] = EVENT_DEFAULT_LENGTH;
	EventMap[0xde] = EVENT_LOOP_UNTIL;
	EventMap[0xdf] = EVENT_LOOP_UNTIL_ALT;
	EventMap[0xe0] = EVENT_RET;
	EventMap[0xe1] = EVENT_CALL;
	EventMap[0xe2] = EVENT_GOTO;
	EventMap[0xe3] = EVENT_TRANSPOSE;
	EventMap[0xe4] = EVENT_TUNING;
	EventMap[0xe5] = EVENT_VIBRATO_DELAY;
	EventMap[0xe6] = EVENT_VIBRATO_OFF;
	EventMap[0xe7] = EVENT_VIBRATO;
	EventMap[0xe8] = EVENT_UNKNOWN1;
	EventMap[0xe9] = EVENT_PITCH_SLIDE;
	EventMap[0xea] = EVENT_VOLUME_REL;
	EventMap[0xeb] = EVENT_PAN;
	EventMap[0xec] = EVENT_VOLUME;
	EventMap[0xed] = EVENT_UNKNOWN_EVENT_ED;
	EventMap[0xee] = EVENT_REST;
	EventMap[0xef] = EVENT_GAIN_ENVELOPE_REST;
	EventMap[0xf0] = EVENT_GAIN_ENVELOPE_DECAY_TIME;
	EventMap[0xf1] = EVENT_MANUAL_DURATION_OFF;
	EventMap[0xf2] = EVENT_MANUAL_DURATION_ON;
	EventMap[0xf3] = EVENT_AUTO_DURATION_THRESHOLD;
	EventMap[0xf4] = EVENT_TIE_WITH_DUR;
	EventMap[0xf5] = EVENT_TIE;
	EventMap[0xf6] = EVENT_GAIN_ENVELOPE_SUSTAIN;
	EventMap[0xf7] = EVENT_ECHO_VOLUME_ENVELOPE;
	EventMap[0xf8] = EVENT_ECHO_VOLUME;
	EventMap[0xf9] = EVENT_ECHO_OFF;
	EventMap[0xfa] = EVENT_ECHO_ON;
	EventMap[0xfb] = EVENT_ECHO_PARAM;
	EventMap[0xfc] = EVENT_ADSR;
	EventMap[0xfd] = EVENT_GAIN_ENVELOPE_DECAY;
	EventMap[0xfe] = EVENT_INSTRUMENT;
	EventMap[0xff] = EVENT_END;

	if (version == PRISMSNES_CGV) {
		EventMap[0xc0] = EventMap[0xd0];
		EventMap[0xc1] = EventMap[0xd0];
		EventMap[0xc2] = EventMap[0xd0];
		EventMap[0xc3] = EventMap[0xd0];
		EventMap[0xc4] = EventMap[0xd0];
		EventMap[0xc5] = EventMap[0xd0];
		EventMap[0xc6] = EventMap[0xd0];
		EventMap[0xc7] = EventMap[0xd0];
		EventMap[0xc8] = EventMap[0xd0];
		EventMap[0xc9] = EventMap[0xd0];
		EventMap[0xca] = EventMap[0xd0];
		EventMap[0xcb] = EventMap[0xd0];
		EventMap[0xcc] = EventMap[0xd0];
		EventMap[0xcd] = EventMap[0xd0];
		EventMap[0xce] = EventMap[0xd0];
		EventMap[0xcf] = EventMap[0xd0];
		EventMap[0xdb] = EVENT_UNKNOWN2;
	}
	else if (version == PRISMSNES_DO) {
		EventMap[0xc0] = EventMap[0xc5];
		EventMap[0xc1] = EventMap[0xc5];
		EventMap[0xc2] = EventMap[0xc5];
		EventMap[0xc3] = EventMap[0xc5];
		EventMap[0xc4] = EventMap[0xc5];
	}
}

double PrismSnesSeq::GetTempoInBPM(uint8_t tempo)
{
	if (tempo != 0) {
		return 60000000.0 / (SEQ_PPQN * (125 * tempo));
	}
	else {
		return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
	}
}

//  **************
//  PrismSnesTrack
//  **************

PrismSnesTrack::PrismSnesTrack(PrismSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void PrismSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	panTable.assign(std::begin(PrismSnesSeq::PAN_TABLE_1), std::end(PrismSnesSeq::PAN_TABLE_1));

	vel = 100;
	defaultLength = 0;
	slur = false;
	manualDuration = false;
	prevNoteSlurred = false;
	prevNoteKey = -1;
	spcVolume = 0;
	loopCount = 0;
	loopCountAlt = 0;
	subReturnAddr = 0;
}

bool PrismSnesTrack::ReadEvent(void)
{
	PrismSnesSeq* parentSeq = (PrismSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	PrismSnesSeqEventType eventType = (PrismSnesSeqEventType)0;
	std::map<uint8_t, PrismSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
	if (pEventType != parentSeq->EventMap.end()) {
		eventType = pEventType->second;
	}

	switch (eventType)
	{
	case EVENT_UNKNOWN0:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
		break;

	case EVENT_UNKNOWN1:
	{
		uint8_t arg1 = GetByte(curOffset++);
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
			<< std::dec << std::setfill(L' ') << std::setw(0)
			<< L"  Arg1: " << (int)arg1;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
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
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
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
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
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
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
		break;
	}

	case EVENT_NOP2:
	{
		curOffset += 2;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"NOP", desc.str(), CLR_MISC, ICON_BINARY);
		break;
	}

	case EVENT_NOTE:
	case EVENT_NOISE_NOTE:
	{
		uint8_t key = statusByte & 0x7f;

		uint8_t len;
		if (!ReadDeltaTime(curOffset, len)) {
			return false;
		}

		uint8_t durDelta;
		if (!ReadDuration(curOffset, len, durDelta)) {
			return false;
		}

		uint8_t dur = GetDuration(curOffset, len, durDelta);

		if (slur) {
			prevNoteSlurred = true;
		}

		if (prevNoteSlurred && key == prevNoteKey) {
			MakePrevDurNoteEnd(GetTime() + dur);
			desc << L"Abs Key: " << key << L" (" << MidiEvent::GetNoteName(key) << L"  Velocity: " << vel << L"  Duration: " << dur;
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Note (Tied)", desc.str(), CLR_DURNOTE, ICON_NOTE);
		}
		else {
			if (eventType == EVENT_NOISE_NOTE) {
				AddNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur, L"Noise Note");
			}
			else {
				AddNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur, L"Note");
			}
		}
		AddTime(len);

		prevNoteKey = key;
		prevNoteSlurred = false;
		break;
	}

	case EVENT_PITCH_SLIDE:
	{
		uint8_t noteFrom = GetByte(curOffset++);
		uint8_t noteTo = GetByte(curOffset++);

		uint8_t noteNumberFrom = noteFrom & 0x7f;
		uint8_t noteNumberTo = noteTo & 0x7f;

		uint8_t len;
		if (!ReadDeltaTime(curOffset, len)) {
			return false;
		}

		uint8_t durDelta;
		if (!ReadDuration(curOffset, len, durDelta)) {
			return false;
		}

		uint8_t dur = GetDuration(curOffset, len, durDelta);

		desc << L"Note Number (From): " << noteNumberFrom << L" (" << ((noteFrom & 0x80) != 0 ? L"Noise" : MidiEvent::GetNoteName(noteNumberFrom)) << L")" <<
			L"  Note Number (To): " << noteNumberTo << L" (" << ((noteTo & 0x80) != 0 ? L"Noise" : MidiEvent::GetNoteName(noteNumberTo)) << L")" <<
			L"  Length: " << len << L"  Duration: " << dur;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Slide", desc.str(), CLR_PITCHBEND, ICON_CONTROL);

		AddNoteByDurNoItem(noteNumberFrom, vel, dur);
		AddTime(len);
		break;
	}

	case EVENT_TIE_WITH_DUR:
	{
		uint8_t len;
		if (!ReadDeltaTime(curOffset, len)) {
			return false;
		}

		uint8_t durDelta;
		if (!ReadDuration(curOffset, len, durDelta)) {
			return false;
		}

		uint8_t dur = GetDuration(curOffset, len, durDelta);

		desc << L"Length: " << len << L"  Duration: " << dur;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie with Duration", desc.str(), CLR_TIE, ICON_NOTE);
		MakePrevDurNoteEnd(GetTime() + dur);
		AddTime(len);
		break;
	}

	case EVENT_TIE:
	{
		prevNoteSlurred = true;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str(), CLR_TIE, ICON_NOTE);
		break;
	}

	case EVENT_UNKNOWN_EVENT_ED:
	{
		uint8_t arg1 = GetByte(curOffset++);
		uint16_t arg2 = GetShort(curOffset); curOffset += 2;
		desc << L"Arg1: " << arg1 << L"  Arg2: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << arg2;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str());
		break;
	}

	case EVENT_REST:
	{
		uint8_t len;
		if (!ReadDeltaTime(curOffset, len)) {
			return false;
		}

		desc << L"Duration: " << len;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Rest", desc.str(), CLR_REST, ICON_REST);
		AddTime(len);
		break;
	}

	case EVENT_TEMPO:
	{
		uint8_t newTempo = GetByte(curOffset++);
		AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
		break;
	}

	case EVENT_CONDITIONAL_JUMP:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Conditional Jump", desc.str(), CLR_LOOP);

		if (parentSeq->conditionSwitch) {
			curOffset = dest;
		}
		break;
	}

	case EVENT_CONDITION:
	{
		parentSeq->conditionSwitch = true;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Condition On", desc.str(), CLR_CHANGESTATE);
		break;
	}

	case EVENT_RESTORE_ECHO_PARAM:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Restore Echo Param", desc.str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_SAVE_ECHO_PARAM:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Save Echo Param", desc.str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_SLUR_OFF:
	{
		slur = false;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Slur Off", desc.str(), CLR_PORTAMENTO, ICON_CONTROL);
		break;
	}

	case EVENT_SLUR_ON:
	{
		slur = true;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Slur On", desc.str(), CLR_PORTAMENTO, ICON_CONTROL);
		break;
	}

	case EVENT_VOLUME_ENVELOPE:
	{
		uint16_t envelopeAddress = GetShort(curOffset); curOffset += 2;
		desc << L"Envelope: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << envelopeAddress;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Volume Envelope", desc.str(), CLR_VOLUME, ICON_CONTROL);
		AddVolumeEnvelope(envelopeAddress);
		break;
	}

	case EVENT_DEFAULT_PAN_TABLE_1:
	{
		panTable.assign(std::begin(PrismSnesSeq::PAN_TABLE_1), std::end(PrismSnesSeq::PAN_TABLE_1));
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Default Pan Table #1", desc.str(), CLR_PAN, ICON_CONTROL);
		break;
	}

	case EVENT_DEFAULT_PAN_TABLE_2:
	{
		panTable.assign(std::begin(PrismSnesSeq::PAN_TABLE_2), std::end(PrismSnesSeq::PAN_TABLE_2));
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Default Pan Table #2", desc.str(), CLR_PAN, ICON_CONTROL);
		break;
	}

	case EVENT_INC_APU_PORT_3:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Increment APU Port 3", desc.str(), CLR_CHANGESTATE);
		break;
	}

	case EVENT_INC_APU_PORT_2:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Increment APU Port 2", desc.str(), CLR_CHANGESTATE);
		break;
	}

	case EVENT_PLAY_SONG_3:
	{
		uint8_t songIndex = GetByte(curOffset++);
		desc << L"Song Index: " << songIndex;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Play Song (3)", desc.str(), CLR_CHANGESTATE);
		break;
	}

	case EVENT_PLAY_SONG_2:
	{
		uint8_t songIndex = GetByte(curOffset++);
		desc << L"Song Index: " << songIndex;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Play Song (2)", desc.str(), CLR_CHANGESTATE);
		break;
	}

	case EVENT_PLAY_SONG_1:
	{
		uint8_t songIndex = GetByte(curOffset++);
		desc << L"Song Index: " << songIndex;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Play Song (1)", desc.str(), CLR_CHANGESTATE);
		break;
	}

	case EVENT_TRANSPOSE_REL:
	{
		int8_t delta = GetByte(curOffset++);
		AddTranspose(beginOffset, curOffset - beginOffset, transpose + delta, L"Transpose (Relative)");
		break;
	}

	case EVENT_PAN_ENVELOPE:
	{
		uint16_t envelopeAddress = GetShort(curOffset); curOffset += 2;
		uint16_t envelopeSpeed = GetByte(curOffset++);
		desc << L"Envelope: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << envelopeAddress << L"  Speed: " << std::dec << std::setfill(L' ') << std::setw(0) << envelopeSpeed;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan Envelope", desc.str(), CLR_PAN, ICON_CONTROL);
		AddPanEnvelope(envelopeAddress);
		break;
	}

	case EVENT_PAN_TABLE:
	{
		uint16_t panTableAddress = GetShort(curOffset); curOffset += 2;
		desc << L"Pan Table: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << panTableAddress;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan Table", desc.str(), CLR_PAN, ICON_CONTROL);

		// update pan table
		if (panTableAddress + 21 <= 0x10000) {
			uint8_t newPanTable[21];
			GetBytes(panTableAddress, 21, newPanTable);
			panTable.assign(std::begin(newPanTable), std::end(newPanTable));
		}

		AddPanTable(panTableAddress);
		break;
	}

	case EVENT_DEFAULT_LENGTH_OFF:
	{
		defaultLength = 0;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Default Length Off", desc.str(), CLR_DURNOTE);
		break;
	}

	case EVENT_DEFAULT_LENGTH:
	{
		defaultLength = GetByte(curOffset++);
		desc << L"Duration: " << defaultLength;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Default Length", desc.str(), CLR_DURNOTE);
		break;
	}

	case EVENT_LOOP_UNTIL:
	{
		uint8_t count = GetByte(curOffset++);
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Loop Count: " << count << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Until", desc.str(), CLR_LOOP, ICON_ENDREP);

		bool doJump;
		if (loopCount == 0) {
			loopCount = count;
			doJump = true;
		}
		else {
			loopCount--;
			if (loopCount != 0) {
				doJump = true;
			}
			else {
				doJump = false;
			}
		}

		if (doJump) {
			curOffset = dest;
		}

		break;
	}

	case EVENT_LOOP_UNTIL_ALT:
	{
		uint8_t count = GetByte(curOffset++);
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Loop Count: " << count << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Until (Alt)", desc.str(), CLR_LOOP, ICON_ENDREP);

		bool doJump;
		if (loopCountAlt == 0) {
			loopCountAlt = count;
			doJump = true;
		}
		else {
			loopCountAlt--;
			if (loopCountAlt != 0) {
				doJump = true;
			}
			else {
				doJump = false;
			}
		}

		if (doJump) {
			curOffset = dest;
		}

		break;
	}

	case EVENT_RET:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern End", desc.str(), CLR_LOOP, ICON_ENDREP);

		if (subReturnAddr != 0) {
			curOffset = subReturnAddr;
			subReturnAddr = 0;
		}

		break;
	}

	case EVENT_CALL:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern Play", desc.str(), CLR_LOOP, ICON_STARTREP);

		subReturnAddr = curOffset;
		curOffset = dest;

		break;
	}

	case EVENT_GOTO:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		uint32_t length = curOffset - beginOffset;

		if (curOffset < 0x10000 && GetByte(curOffset) == 0xff) {
			AddGenericEvent(curOffset, 1, L"End of Track", L"", CLR_TRACKEND, ICON_TRACKEND);
		}

		curOffset = dest;
		if (!IsOffsetUsed(dest)) {
			AddGenericEvent(beginOffset, length, L"Jump", desc.str(), CLR_LOOPFOREVER);
		}
		else {
			bContinue = AddLoopForever(beginOffset, length, L"Jump");
		}

		if (curOffset < dwOffset) {
			dwOffset = curOffset;
		}

		break;
	}

	case EVENT_TRANSPOSE:
	{
		int8_t newTranspose = GetByte(curOffset++);
		AddTranspose(beginOffset, curOffset - beginOffset, newTranspose);
		break;
	}

	case EVENT_TUNING:
	{
		uint8_t newTuning = GetByte(curOffset++);
		double semitones = newTuning / 256.0;
		AddFineTuning(beginOffset, curOffset - beginOffset, semitones * 100.0);
		break;
	}

	case EVENT_VIBRATO_DELAY:
	{
		uint8_t lfoDelay = GetByte(curOffset++);
		desc << L"Delay: " << (int)lfoDelay;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Delay", desc.str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_VIBRATO_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Off", desc.str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_VIBRATO:
	{
		uint8_t lfoDelay = GetByte(curOffset++);
		uint8_t arg2 = GetByte(curOffset++);
		uint8_t arg3 = GetByte(curOffset++);
		desc << L"Delay: " << (int)lfoDelay << L"  Arg2: " << (int)arg2 << L"  Arg3: " << (int)arg3;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato", desc.str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_VOLUME_REL:
	{
		int8_t delta = GetByte(curOffset++);
		if (spcVolume + delta > 255) {
			spcVolume = 255;
		}
		else if (spcVolume + delta < 0) {
			spcVolume = 0;
		}
		else {
			spcVolume += delta;
		}
		AddVol(beginOffset, curOffset - beginOffset, spcVolume / 2, L"Volume (Relative)");
		break;
	}

	case EVENT_PAN:
	{
		uint8_t newPan = GetByte(curOffset++);
		if (newPan > 20) {
			// unexpected value
			newPan = 20;
		}

		// TODO: use correct pan table
		double volumeLeft;
		double volumeRight;
		// actual engine divides pan by 256, though pan value must be always 7-bit, perhaps
		volumeLeft = panTable[20 - newPan] / 128.0;
		volumeRight = panTable[newPan] / 128.0;

		// TODO: fix volume scale when L+R > 1.0?
		double volumeScale;
		int8_t midiPan = ConvertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &volumeScale);
		volumeScale = min(volumeScale, 1.0); // workaround

		AddPan(beginOffset, curOffset - beginOffset, midiPan);
		AddExpressionNoItem(ConvertPercentAmpToStdMidiVal(volumeScale));
		break;
	}

	case EVENT_VOLUME:
	{
		uint8_t newVolume = GetByte(curOffset++);
		spcVolume = newVolume;
		AddVol(beginOffset, curOffset - beginOffset, spcVolume / 2);
		break;
	}

	case EVENT_GAIN_ENVELOPE_REST:
	{
		uint16_t envelopeAddress = GetShort(curOffset); curOffset += 2;
		desc << L"Envelope: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << envelopeAddress;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"GAIN Envelope (Rest)", desc.str(), CLR_ADSR, ICON_CONTROL);
		AddGAINEnvelope(envelopeAddress);
		break;
	}

	case EVENT_GAIN_ENVELOPE_DECAY_TIME:
	{
		uint8_t dur = GetByte(curOffset++);
		uint8_t gain = GetByte(curOffset++);
		desc << L"Duration: Full-Length - " << dur << L"  GAIN: $" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << gain;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"GAIN Envelope Decay Time", desc.str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_MANUAL_DURATION_OFF:
	{
		manualDuration = false;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Manual Duration Off", desc.str(), CLR_DURNOTE);
		break;
	}

	case EVENT_MANUAL_DURATION_ON:
	{
		manualDuration = true;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Manual Duration On", desc.str(), CLR_DURNOTE);
		break;
	}

	case EVENT_AUTO_DURATION_THRESHOLD:
	{
		manualDuration = false;
		autoDurationThreshold = GetByte(curOffset++);
		desc << L"Duration: Full-Length - " << autoDurationThreshold;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Auto Duration Threshold", desc.str(), CLR_DURNOTE);
		break;
	}

	case EVENT_GAIN_ENVELOPE_SUSTAIN:
	{
		uint16_t envelopeAddress = GetShort(curOffset); curOffset += 2;
		desc << L"Envelope: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << envelopeAddress;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"GAIN Envelope (Sustain)", desc.str(), CLR_ADSR, ICON_CONTROL);
		AddGAINEnvelope(envelopeAddress);
		break;
	}

	case EVENT_ECHO_VOLUME_ENVELOPE:
	{
		uint16_t envelopeAddress = GetShort(curOffset); curOffset += 2;
		desc << L"Envelope: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << envelopeAddress;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Volume Envelope", desc.str(), CLR_REVERB, ICON_CONTROL);
		AddEchoVolumeEnvelope(envelopeAddress);
		break;
	}

	case EVENT_ECHO_VOLUME:
	{
		int8_t echoVolumeLeft = GetByte(curOffset++);
		int8_t echoVolumeRight = GetByte(curOffset++);
		int8_t echoVolumeMono = GetByte(curOffset++);
		desc << L"Left Volume: " << echoVolumeLeft << L"  Right Volume: " << echoVolumeRight << L"  Mono Volume: " << echoVolumeMono;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Volume", desc.str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_OFF:
	{
		AddReverb(beginOffset, curOffset - beginOffset, 0, L"Echo Off");
		break;
	}

	case EVENT_ECHO_ON:
	{
		AddReverb(beginOffset, curOffset - beginOffset, 40, L"Echo On");
		break;
	}

	case EVENT_ECHO_PARAM:
	{
		int8_t echoFeedback = GetByte(curOffset++);
		int8_t echoVolumeLeft = GetByte(curOffset++);
		int8_t echoVolumeRight = GetByte(curOffset++);
		int8_t echoVolumeMono = GetByte(curOffset++);
		desc << L"Feedback: " << echoFeedback << L"  Left Volume: " << echoVolumeLeft << L"  Right Volume: " << echoVolumeRight << L"  Mono Volume: " << echoVolumeMono;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Param", desc.str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ADSR:
	{
		uint8_t param = GetByte(curOffset);
		if (param >= 0x80) {
			uint8_t adsr1 = GetByte(curOffset++);
			uint8_t adsr2 = GetByte(curOffset++);
			desc << L"ADSR(1): $" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << adsr1 <<
				L"  ADSR(2): $" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << adsr2;
		}
		else {
			uint8_t instrNum = GetByte(curOffset++);
			desc << L"Instrument: " << instrNum;
		}
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR", desc.str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_GAIN_ENVELOPE_DECAY:
	{
		uint16_t envelopeAddress = GetShort(curOffset); curOffset += 2;
		desc << L"Envelope: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << envelopeAddress;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"GAIN Envelope (Decay)", desc.str(), CLR_ADSR, ICON_CONTROL);
		AddGAINEnvelope(envelopeAddress);
		break;
	}

	case EVENT_INSTRUMENT:
	{
		uint8_t instrNum = GetByte(curOffset++);
		AddProgramChange(beginOffset, curOffset - beginOffset, instrNum, true);
		break;
	}

	case EVENT_END:
	{
		AddEndOfTrack(beginOffset, curOffset - beginOffset);
		bContinue = false;
		break;
	}

	default:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str());
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"PrismSnesSeq")));
		bContinue = false;
		break;
	}

	//assert(curOffset >= parentSeq->dwOffset);

	//std::wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}

bool PrismSnesTrack::ReadDeltaTime(uint32_t& curOffset, uint8_t& len)
{
	if (curOffset + 1 > 0x10000) {
		return false;
	}

	if (defaultLength != 0) {
		len = defaultLength;
	}
	else {
		len = GetByte(curOffset++);
	}
	return true;
}

bool PrismSnesTrack::ReadDuration(uint32_t& curOffset, uint8_t len, uint8_t& durDelta)
{
	if (curOffset + 1 > 0x10000) {
		return false;
	}

	if (manualDuration) {
		durDelta = GetByte(curOffset++);
	}
	else {
		durDelta = len >> 1;
		if (durDelta > autoDurationThreshold) {
			durDelta = autoDurationThreshold;
		}
	}

	if (durDelta > len) {
		// unexpected value
		durDelta = 0;
	}

	return true;
}

uint8_t PrismSnesTrack::GetDuration(uint32_t curOffset, uint8_t len, uint8_t durDelta)
{
	// TODO: adjust duration for slur/tie
	if (curOffset + 1 <= 0x10000) {
		uint8_t nextStatusByte = GetByte(curOffset);
		if (nextStatusByte == 0xee || nextStatusByte == 0xf4 || nextStatusByte == 0xce || nextStatusByte == 0xf5) {
			durDelta = 0;
		}
	}

	if (durDelta > len) {
		// unexpected value
		durDelta = 0;
	}

	return len - durDelta;
}

void PrismSnesTrack::AddVolumeEnvelope(uint16_t envelopeAddress)
{
	PrismSnesSeq* parentSeq = (PrismSnesSeq*)this->parentSeq;
	parentSeq->DemandEnvelopeContainer(envelopeAddress);

	if (!IsOffsetUsed(envelopeAddress)) {
		std::wostringstream envelopeName;
		envelopeName << L"Volume Envelope ($" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << envelopeAddress << L")";
		VGMHeader* envHeader = parentSeq->envContainer->AddHeader(envelopeAddress, 0, envelopeName.str());

		uint16_t envOffset = 0;
		while (envelopeAddress + envOffset + 2 <= 0x10000) {
			std::wostringstream eventName;

			uint8_t volumeFrom = GetByte(envelopeAddress + envOffset);
			int8_t volumeDelta = GetByte(envelopeAddress + envOffset + 1);
			if (volumeFrom == 0 && volumeDelta == 0) {
				// $00 $00 $xx $yy sets offset to $yyxx
				uint16_t newAddress = GetByte(envelopeAddress + envOffset + 2);

				eventName << L"Jump to $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << newAddress;
				envHeader->AddSimpleItem(envelopeAddress + envOffset, 4, eventName.str());
				envOffset += 4;
				break;
			}

			uint8_t envelopeSpeed = GetByte(envelopeAddress + envOffset + 2);
			uint8_t deltaTime = GetByte(envelopeAddress + envOffset + 3);
			eventName << L"Volume: " << volumeFrom << L"  Volume Delta: " << volumeDelta << L"  Envelope Speed: " << envelopeSpeed << L"  Delta-Time: " << deltaTime;
			envHeader->AddSimpleItem(envelopeAddress + envOffset, 4, eventName.str());
			envOffset += 4;

			// workaround: quit to prevent out of bound
			break;
		}

		envHeader->unLength = envOffset;
	}
}

void PrismSnesTrack::AddPanEnvelope(uint16_t envelopeAddress)
{
	PrismSnesSeq* parentSeq = (PrismSnesSeq*)this->parentSeq;
	parentSeq->DemandEnvelopeContainer(envelopeAddress);

	if (!IsOffsetUsed(envelopeAddress)) {
		std::wostringstream envelopeName;
		envelopeName << L"Pan Envelope ($" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << envelopeAddress << L")";
		VGMHeader* envHeader = parentSeq->envContainer->AddHeader(envelopeAddress, 0, envelopeName.str());

		uint16_t envOffset = 0;
		while (envOffset < 0x100) {
			std::wostringstream eventName;

			uint8_t newPan = GetByte(envelopeAddress + envOffset);
			if (newPan >= 0x80) {
				// $ff $xx sets offset to $xx
				uint8_t newOffset = GetByte(envelopeAddress + envOffset + 1);

				eventName << L"Jump to $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (envelopeAddress + newOffset);
				envHeader->AddSimpleItem(envelopeAddress + envOffset, 2, eventName.str());
				envOffset += 2;
				break;
			}

			eventName << L"Pan: " << newPan;
			envHeader->AddSimpleItem(envelopeAddress + envOffset, 1, eventName.str());
			envOffset++;

			// workaround: quit to prevent out of bound
			break;
		}

		envHeader->unLength = envOffset;
	}
}

void PrismSnesTrack::AddEchoVolumeEnvelope(uint16_t envelopeAddress)
{
	PrismSnesSeq* parentSeq = (PrismSnesSeq*)this->parentSeq;
	parentSeq->DemandEnvelopeContainer(envelopeAddress);

	if (!IsOffsetUsed(envelopeAddress)) {
		std::wostringstream envelopeName;
		envelopeName << L"Echo Volume Envelope ($" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << envelopeAddress << L")";
		VGMHeader* envHeader = parentSeq->envContainer->AddHeader(envelopeAddress, 0, envelopeName.str());

		uint16_t envOffset = 0;
		while (envOffset < 0x100) {
			std::wostringstream eventName;

			uint8_t deltaTime = GetByte(envelopeAddress + envOffset);
			if (deltaTime == 0xff) {
				// $ff $xx sets offset to $xx
				uint8_t newOffset = GetByte(envelopeAddress + envOffset + 1);

				eventName << L"Jump to $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (envelopeAddress + newOffset);
				envHeader->AddSimpleItem(envelopeAddress + envOffset, 2, eventName.str());
				envOffset += 2;
				break;
			}

			int8_t echoVolumeLeft = GetByte(envelopeAddress + envOffset + 1);
			int8_t echoVolumeRight = GetByte(envelopeAddress + envOffset + 2);
			int8_t echoVolumeMono = GetByte(envelopeAddress + envOffset + 3);
			eventName << L"Delta-Time: " << deltaTime << L"  Left Volume: " << echoVolumeLeft << L"  Right Volume: " << echoVolumeRight << L"  Mono Volume: " << echoVolumeMono;
			envHeader->AddSimpleItem(envelopeAddress + envOffset, 4, eventName.str());
			envOffset += 4;

			// workaround: quit to prevent out of bound
			break;
		}

		envHeader->unLength = envOffset;
	}
}

void PrismSnesTrack::AddGAINEnvelope(uint16_t envelopeAddress)
{
	PrismSnesSeq* parentSeq = (PrismSnesSeq*)this->parentSeq;
	parentSeq->DemandEnvelopeContainer(envelopeAddress);

	if (!IsOffsetUsed(envelopeAddress)) {
		std::wostringstream envelopeName;
		envelopeName << L"GAIN Envelope ($" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << envelopeAddress << L")";
		VGMHeader* envHeader = parentSeq->envContainer->AddHeader(envelopeAddress, 0, envelopeName.str());

		uint16_t envOffset = 0;
		while (envOffset < 0x100) {
			std::wostringstream eventName;

			uint8_t gain = GetByte(envelopeAddress + envOffset);
			if (gain == 0xff) {
				// $ff $xx sets offset to $xx
				uint8_t newOffset = GetByte(envelopeAddress + envOffset + 1);

				eventName << L"Jump to $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (envelopeAddress + newOffset);
				envHeader->AddSimpleItem(envelopeAddress + envOffset, 2, eventName.str());
				envOffset += 2;
				break;
			}

			uint8_t deltaTime = GetByte(envelopeAddress + envOffset + 1);
			eventName << L"GAIN: $" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << gain << std::dec << std::setfill(L' ') << std::setw(0) << L", Delta-Time: " << deltaTime;
			envHeader->AddSimpleItem(envelopeAddress + envOffset, 2, eventName.str());
			envOffset += 2;

			// workaround: quit to prevent out of bound
			break;
		}

		envHeader->unLength = envOffset;
	}
}

void PrismSnesTrack::AddPanTable(uint16_t panTableAddress)
{
	PrismSnesSeq* parentSeq = (PrismSnesSeq*)this->parentSeq;
	parentSeq->DemandEnvelopeContainer(panTableAddress);

	if (!IsOffsetUsed(panTableAddress)) {
		std::wostringstream eventName;
		eventName << L"Pan Table ($" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << panTableAddress << L")";
		parentSeq->envContainer->AddSimpleItem(panTableAddress, 21, eventName.str());
	}
}
