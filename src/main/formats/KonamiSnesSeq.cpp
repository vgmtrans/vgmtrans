#include "stdafx.h"
#include "KonamiSnesSeq.h"
#include "KonamiSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(KonamiSnes);

//  **********
//  KonamiSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    48

const uint8_t KonamiSnesSeq::PAN_VOLUME_LEFT_V1[] = {
	0x00, 0x05, 0x0c, 0x14, 0x1e, 0x28, 0x32, 0x3c,
	0x46, 0x50, 0x59, 0x62, 0x69, 0x6f, 0x74, 0x78,
	0x7b, 0x7d, 0x7e, 0x7e, 0x7f
};

const uint8_t KonamiSnesSeq::PAN_VOLUME_RIGHT_V1[] = {
	0x7f, 0x7e, 0x7e, 0x7d, 0x7b, 0x78, 0x74, 0x6f,
	0x69, 0x62, 0x59, 0x50, 0x46, 0x3c, 0x32, 0x28,
	0x1e, 0x14, 0x0c, 0x05, 0x00
};

const uint8_t KonamiSnesSeq::PAN_VOLUME_LEFT_V2[] = {
	0x00, 0x0a, 0x18, 0x28, 0x3c, 0x50, 0x64, 0x78,
	0x8c, 0xa0, 0xb2, 0xc4, 0xd2, 0xde, 0xe8, 0xf0,
	0xf6, 0xfa, 0xfc, 0xfc, 0xfe
};

const uint8_t KonamiSnesSeq::PAN_VOLUME_RIGHT_V2[] = {
	0xfe, 0xfc, 0xfc, 0xfa, 0xf6, 0xf0, 0xe8, 0xde,
	0xd2, 0xc4, 0xb2, 0xa0, 0x8c, 0x78, 0x64, 0x50,
	0x3c, 0x28, 0x18, 0x0a, 0x00
};

// pan table (compatible with Nintendo engine)
const uint8_t KonamiSnesSeq::PAN_TABLE[] = {
	0x00, 0x04, 0x08, 0x0e, 0x14, 0x1a, 0x20, 0x28,
	0x30, 0x38, 0x40, 0x48, 0x50, 0x5a, 0x64, 0x6e,
	0x78, 0x82, 0x8c, 0x96, 0xa0, 0xa8, 0xb0, 0xb8,
	0xc0, 0xc8, 0xd0, 0xd6, 0xdc, 0xe0, 0xe4, 0xe8,
	0xec, 0xf0, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0xfe,
	0xfe, 0xfe
};

KonamiSnesSeq::KonamiSnesSeq(RawFile* file, KonamiSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
: VGMSeq(KonamiSnesFormat::name, file, seqdataOffset), version(ver)
{
	name = newName;

	bAllowDiscontinuousTrackData = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap();
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
		if (trackPointerOffset + 2 > 0x10000) {
			return false;
		}

		uint16_t trkOff = GetShort(trackPointerOffset);
		seqHeader->AddPointer(trackPointerOffset, 2, trkOff, true, L"Track Pointer");

		assert(trkOff >= dwOffset);

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

void KonamiSnesSeq::LoadEventMap()
{
	if (version == KONAMISNES_V1) {
		NOTE_DUR_RATE_MAX = 100;
	}
	else {
		NOTE_DUR_RATE_MAX = 127;
	}

	for (uint8_t statusByte = 0x00; statusByte <= 0x5f; statusByte++) {
		EventMap[statusByte] = EVENT_NOTE;
		EventMap[statusByte | 0x80] = EVENT_NOTE;
	}

	EventMap[0x60] = EVENT_PERCUSSION_ON;
	EventMap[0x61] = EVENT_PERCUSSION_OFF;

	if (version == KONAMISNES_V1) {
		EventMap[0x62] = EVENT_UNKNOWN1;
		EventMap[0x63] = EVENT_UNKNOWN1;
		EventMap[0x64] = EVENT_UNKNOWN2;

		for (uint8_t statusByte = 0x65; statusByte <= 0x7f; statusByte++) {
			EventMap[statusByte] = EVENT_UNKNOWN0;
		}
	}
	else {
		EventMap[0x62] = EVENT_GAIN;

		for (uint8_t statusByte = 0x63; statusByte <= 0x7f; statusByte++) {
			EventMap[statusByte] = EVENT_UNKNOWN0;
		}
	}

	EventMap[0xe0] = EVENT_REST;
	EventMap[0xe1] = EVENT_TIE;
	EventMap[0xe2] = EVENT_PROGCHANGE;
	EventMap[0xe3] = EVENT_PAN;
	EventMap[0xe4] = EVENT_VIBRATO;
	EventMap[0xe5] = EVENT_RANDOM_PITCH;
	EventMap[0xe6] = EVENT_LOOP_START;
	EventMap[0xe7] = EVENT_LOOP_END;
	EventMap[0xe8] = EVENT_LOOP_START_2;
	EventMap[0xe9] = EVENT_LOOP_END_2;
	EventMap[0xea] = EVENT_TEMPO;
	EventMap[0xeb] = EVENT_TEMPO_FADE;
	EventMap[0xec] = EVENT_TRANSPABS;
	EventMap[0xed] = EVENT_ADSR1;
	EventMap[0xee] = EVENT_VOLUME;
	EventMap[0xef] = EVENT_VOLUME_FADE;
	EventMap[0xf0] = EVENT_PORTAMENTO;
	EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V2;
	EventMap[0xf2] = EVENT_TUNING;
	EventMap[0xf3] = EVENT_PITCH_SLIDE_V3;
	EventMap[0xf4] = EVENT_ECHO;
	EventMap[0xf5] = EVENT_ECHO_PARAM;
	EventMap[0xf6] = EVENT_LOOP_WITH_VOLTA_START;
	EventMap[0xf7] = EVENT_LOOP_WITH_VOLTA_END;
	EventMap[0xf8] = EVENT_PAN_FADE;
	EventMap[0xf9] = EVENT_VIBRATO_FADE;
	EventMap[0xfa] = EVENT_ADSR_GAIN;
	EventMap[0xfb] = EVENT_ADSR2;
	EventMap[0xfc] = EVENT_PROGCHANGEVOL;
	EventMap[0xfd] = EVENT_GOTO;
	EventMap[0xfe] = EVENT_CALL;
	EventMap[0xff] = EVENT_END;

	switch(version)
	{
	case KONAMISNES_V1:
		EventMap[0xed] = EVENT_UNKNOWN3; // nop
		EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V1;
		EventMap[0xf3] = EVENT_PITCH_SLIDE_V1;
		EventMap[0xfa] = EVENT_UNKNOWN3;
		EventMap[0xfb] = EVENT_UNKNOWN1;
		EventMap.erase(0xfc); // game-specific?
		break;

	case KONAMISNES_V2:
		EventMap[0xed] = EVENT_UNKNOWN3; // nop
		EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V1;
		EventMap[0xf3] = EVENT_PITCH_SLIDE_V2;
		EventMap[0xfa] = EVENT_UNKNOWN3;
		EventMap[0xfb] = EVENT_UNKNOWN1;
		EventMap.erase(0xfc); // game-specific?
		break;

	case KONAMISNES_V3:
		EventMap[0xed] = EVENT_UNKNOWN3; // nop
		EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V1;
		EventMap[0xf3] = EVENT_PITCH_SLIDE_V2;
		EventMap[0xfa] = EVENT_UNKNOWN3;
		EventMap[0xfb] = EVENT_UNKNOWN1;
		EventMap[0xfc] = EVENT_UNKNOWN2;
		break;

	case KONAMISNES_V4:
		EventMap[0xed] = EVENT_UNKNOWN3;
		EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V2;
		EventMap[0xf3] = EVENT_PITCH_SLIDE_V2;
		EventMap[0xfa] = EVENT_ADSR_GAIN;
		EventMap[0xfb] = EVENT_ADSR2;
		EventMap[0xfc] = EVENT_PROGCHANGEVOL;
		break;

	case KONAMISNES_V5:
		for (uint8_t statusByte = 0x70; statusByte <= 0x7f; statusByte++) {
			EventMap[statusByte] = EVENT_INSTANT_TUNING;
		}

		EventMap[0xed] = EVENT_ADSR1;
		EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V2;
		EventMap[0xf3] = EVENT_PITCH_SLIDE_V3;
		EventMap[0xfa] = EVENT_ADSR_GAIN;
		EventMap[0xfb] = EVENT_ADSR2;
		EventMap[0xfc] = EVENT_PROGCHANGEVOL;
		break;

	case KONAMISNES_V6:
		for (uint8_t statusByte = 0x70; statusByte <= 0x7f; statusByte++) {
			EventMap[statusByte] = EVENT_INSTANT_TUNING;
		}

		EventMap[0xed] = EVENT_ADSR1;
		EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V2;
		EventMap[0xf3] = EVENT_PITCH_SLIDE_V3;
		EventMap[0xfa] = EVENT_ADSR_GAIN;
		EventMap[0xfb] = EVENT_ADSR2;
		EventMap[0xfc] = EVENT_PROGCHANGEVOL;
		break;
	}
}

double KonamiSnesSeq::GetTempoInBPM ()
{
	return GetTempoInBPM(tempo);
}

double KonamiSnesSeq::GetTempoInBPM (uint8_t tempo)
{
	if (tempo != 0) {
		uint8_t timerFreq;
		if (version == KONAMISNES_V1) {
			timerFreq = 0x20;
		}
		else {
			timerFreq = 0x40;
		}

		return 60000000.0 / (SEQ_PPQN * (125 * timerFreq)) * (tempo / 256.0);
	}
	else {
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

void KonamiSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	inSubroutine = false;
	loopCount = 0;
	loopVolumeDelta = 0;
	loopPitchDelta = 0;
	loopCount2 = 0;
	loopVolumeDelta2 = 0;
	loopPitchDelta2 = 0;
	voltaEndMeansPlayFromStart = false;
	voltaEndMeansPlayNextVolta = false;
	percussion = false;

	noteLength = 0;
	noteDurationRate = 0;
	subReturnAddr = 0;
	loopReturnAddr = 0;
	loopReturnAddr2 = 0;
	voltaLoopStart = 0;
	voltaLoopEnd = 0;
	instrument = 0;

	prevNoteKey = -1;
	prevNoteSlurred = false;
}

double KonamiSnesTrack::GetTuningInSemitones(int8_t tuning)
{
	return tuning * 4 / 256.0;
}


uint8_t KonamiSnesTrack::ConvertGAINAmountToGAIN(uint8_t gainAmount)
{
	uint8_t gain = gainAmount;
	if (gainAmount >= 200)
	{
		// exponential decrease
		gain = 0x80 | ((gainAmount - 200) & 0x1f);
	}
	else if (gainAmount >= 100)
	{
		// linear decrease
		gain = 0xa0 | ((gainAmount - 100) & 0x1f);
	}
	return gain;
}

bool KonamiSnesTrack::ReadEvent(void)
{
	KonamiSnesSeq* parentSeq = (KonamiSnesSeq*)this->parentSeq;
	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000)
	{
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	KonamiSnesSeqEventType eventType = (KonamiSnesSeqEventType)0;
	std::map<uint8_t, KonamiSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
	if (pEventType != parentSeq->EventMap.end())
	{
		eventType = pEventType->second;
	}

	switch (eventType)
	{
	case EVENT_UNKNOWN0:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
		break;

	case EVENT_UNKNOWN1:
	{
		uint8_t arg1 = GetByte(curOffset++);
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
			<< std::dec << std::setfill(L' ') << std::setw(0)
			<< L"  Arg1: " << (int)arg1;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
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
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
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
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
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
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
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
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
		break;
	}

	case EVENT_NOTE:
	{
		bool hasNoteLength = ((statusByte & 0x80) == 0);
		uint8_t key = statusByte & 0x7f;

		uint8_t len;
		if (hasNoteLength) {
			len = GetByte(curOffset++);
			noteLength = len;
		}
		else {
			len = noteLength;
		}

		uint8_t vel;
		vel = GetByte(curOffset++);
		bool hasNoteDuration = ((vel & 0x80) == 0);
		if (hasNoteDuration) {
			noteDurationRate = min(vel, parentSeq->NOTE_DUR_RATE_MAX);
			vel = GetByte(curOffset++);
		}
		vel &= 0x7f;

		if (vel == 0) {
			vel = 1; // TODO: verification
		}

		uint8_t dur = len;
		if (noteDurationRate != parentSeq->NOTE_DUR_RATE_MAX) {
			if (parentSeq->version == KONAMISNES_V1) {
				dur = (len * noteDurationRate) / 100;
			}
			else {
				dur = (len * (noteDurationRate << 1)) >> 8;
			}

			if (dur == 0) {
				dur = 1;
			}
		}

		if (prevNoteSlurred && key == prevNoteKey) {
			// TODO: Note volume can be changed during a tied note
			// See the end of Konami Logo sequence for example
			MakePrevDurNoteEnd(GetTime() + dur);
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str().c_str(), CLR_TIE, ICON_NOTE);
		}
		else {
			AddNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur);
			prevNoteKey = key;
		}
		prevNoteSlurred = (noteDurationRate == parentSeq->NOTE_DUR_RATE_MAX);
		AddTime(len);

		break;
	}

	case EVENT_PERCUSSION_ON:
	{
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Percussion On", desc.str().c_str(), CLR_CHANGESTATE);
		if (!percussion) {
			AddProgramChange(beginOffset, curOffset-beginOffset, 127 << 7, true);
			percussion = true;
		}
		break;
	}

	case EVENT_PERCUSSION_OFF:
	{
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Percussion Off", desc.str().c_str(), CLR_CHANGESTATE);
		if (percussion) {
			AddProgramChange(beginOffset, curOffset-beginOffset, instrument, true);
			percussion = false;
		}
		break;
	}

	case EVENT_GAIN:
	{
		uint8_t newGAINAmount = GetByte(curOffset++);
		uint8_t newGAIN = ConvertGAINAmountToGAIN(newGAINAmount);

		desc << L"GAIN: " << (int)newGAINAmount << L" ($" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)newGAIN << L")";
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"GAIN", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_INSTANT_TUNING:
	{
		int8_t newTuning = statusByte & 0x0f;
		if (newTuning > 8) {
			// extend sign
			newTuning -= 16;
		}

		double cents = GetTuningInSemitones(newTuning) * 100.0;
		AddFineTuning(beginOffset, curOffset - beginOffset, cents, L"Instant Fine Tuning");
		break;
	}

	case EVENT_REST:
	{
		noteLength = GetByte(curOffset++);
		AddRest(beginOffset, curOffset-beginOffset, noteLength);
		prevNoteSlurred = false;
		break;
	}

	case EVENT_TIE:
	{
		noteLength = GetByte(curOffset++);
		noteDurationRate = GetByte(curOffset++);
		noteDurationRate = min(noteDurationRate, parentSeq->NOTE_DUR_RATE_MAX);
		if (prevNoteSlurred) {
			uint8_t dur = noteLength;
			if (noteDurationRate < parentSeq->NOTE_DUR_RATE_MAX) {
				if (parentSeq->version == KONAMISNES_V1) {
					dur = (noteLength * noteDurationRate) / 100;
				}
				else {
					dur = (noteLength * (noteDurationRate << 1)) >> 8;
				}

				if (dur == 0) {
					dur = 1;
				}
			}

			MakePrevDurNoteEnd(GetTime() + dur);
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str().c_str(), CLR_TIE, ICON_NOTE);
			AddTime(noteLength);
			prevNoteSlurred = (noteDurationRate == parentSeq->NOTE_DUR_RATE_MAX);
		}
		else {
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str().c_str(), CLR_TIE, ICON_NOTE);
			AddTime(noteLength);
		}
		break;
	}

	case EVENT_PROGCHANGE:
	{
		uint8_t newProg = GetByte(curOffset++);

		instrument = newProg;
		AddProgramChange(beginOffset, curOffset-beginOffset, newProg, true);
		AddPanNoItem(64); // TODO: apply true pan from instrument table
		break;
	}

	case EVENT_PROGCHANGEVOL:
	{
		uint8_t newVolume = GetByte(curOffset++);
		uint8_t newProg = GetByte(curOffset++);

		instrument = newProg;
		AddProgramChange(beginOffset, curOffset-beginOffset, newProg, true);

		uint8_t midiVolume = ConvertPercentAmpToStdMidiVal(newVolume / 255.0);
		AddVolNoItem(midiVolume);
		AddPanNoItem(64); // TODO: apply true pan from instrument table
		break;
	}

	case EVENT_PAN:
	{
		uint8_t newPan = GetByte(curOffset++);

		bool instrumentPanOff;
		bool instrumentPanOn;
		switch (parentSeq->version) {
		case KONAMISNES_V1:
		case KONAMISNES_V2:
			instrumentPanOff = (newPan == 0x15);
			instrumentPanOn = (newPan == 0x16);
			break;

		default:
			instrumentPanOff = (newPan == 0x2a);
			instrumentPanOn = (newPan == 0x2c);
		}

		if (instrumentPanOff) {
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Per-Instrument Pan Off", desc.str().c_str(), CLR_PAN, ICON_CONTROL);
		}
		else if (instrumentPanOn) {
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Per-Instrument Pan On", desc.str().c_str(), CLR_PAN, ICON_CONTROL);
		}
		else {
			uint8_t midiPan;
			double midiScalePan;

			switch (parentSeq->version) {
			case KONAMISNES_V1:
			case KONAMISNES_V2:
			{
				const uint8_t* PAN_VOLUME_LEFT;
				const uint8_t* PAN_VOLUME_RIGHT;
				if (parentSeq->version == KONAMISNES_V1) {
					PAN_VOLUME_LEFT = parentSeq->PAN_VOLUME_LEFT_V1;
					PAN_VOLUME_RIGHT = parentSeq->PAN_VOLUME_RIGHT_V1;
				}
				else { // KONAMISNES_V2
					PAN_VOLUME_LEFT = parentSeq->PAN_VOLUME_LEFT_V2;
					PAN_VOLUME_RIGHT = parentSeq->PAN_VOLUME_RIGHT_V2;
				}

				newPan = min(newPan, 20);
				uint8_t volumeLeft = PAN_VOLUME_LEFT[newPan];
				uint8_t volumeRight = PAN_VOLUME_RIGHT[newPan];
				double linearPan = (double)volumeRight / (volumeLeft + volumeRight);
				midiScalePan = ConvertPercentPanToStdMidiScale(linearPan);
				break;
			}

			default:
				newPan = min(newPan, 40);
				midiScalePan = ConvertPercentPanToStdMidiScale(KonamiSnesSeq::PAN_TABLE[40 - newPan] / 256.0);
			}

			if (midiScalePan == 0.0) {
				midiPan = 0;
			}
			else {
				midiPan = 1 + roundi(midiScalePan * 126.0);
			}

			// TODO: apply volume scale
			AddPan(beginOffset, curOffset - beginOffset, midiPan);
		}
		break;
	}

	case EVENT_VIBRATO:
	{
		uint8_t vibratoDelay = GetByte(curOffset++);
		uint8_t vibratoRate = GetByte(curOffset++);
		uint8_t vibratoDepth = GetByte(curOffset++);
		desc << L"Delay: " << (int)vibratoDelay << L"  Rate: " << (int)vibratoRate << L"  Depth: " << (int)vibratoDepth;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_RANDOM_PITCH:
	{
		uint8_t envRate = GetByte(curOffset++);
		uint16_t envPitchMask = GetShort(curOffset); curOffset += 2;
		desc << L"Rate: " << (int)envRate << L"  Pitch Mask: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)envPitchMask;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Random Pitch", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_LOOP_START:
	{
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop Start", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);
		loopReturnAddr = curOffset;
		break;
	}

	case EVENT_LOOP_END:
	{
		uint8_t times = GetByte(curOffset++);
		int8_t volumeDelta = GetByte(curOffset++);
		int8_t pitchDelta = GetByte(curOffset++);

		desc << L"Times: " << (int)times << L"  Volume Delta: " << (int)volumeDelta << L"  Pitch Delta: " << (int)pitchDelta;
		if (times == 0) {
			bContinue = AddLoopForever(beginOffset, curOffset - beginOffset, L"Loop End");
		}
		else {
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop End", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);
		}

		bool loopAgain;
		if (times == 0) {
			// infinite loop
			loopAgain = true;
		}
		else {
			loopCount++;
			loopAgain = (loopCount != times);
		}

		if (loopAgain) {
			curOffset = loopReturnAddr;
			loopVolumeDelta += volumeDelta;
			loopPitchDelta += pitchDelta;

			assert(loopReturnAddr != 0);
		}
		else {
			loopCount = 0;
			loopVolumeDelta = 0;
			loopPitchDelta = 0;
		}
		break;
	}

	case EVENT_LOOP_START_2:
	{
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop Start #2", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);
		loopReturnAddr2 = curOffset;
		break;
	}

	case EVENT_LOOP_END_2:
	{
		uint8_t times = GetByte(curOffset++);
		int8_t volumeDelta = GetByte(curOffset++);
		int8_t pitchDelta = GetByte(curOffset++);

		desc << L"Times: " << (int)times << L"  Volume Delta: " << (int)volumeDelta << L"  Pitch Delta: " << (int)pitchDelta;
		if (times == 0) {
			bContinue = AddLoopForever(beginOffset, curOffset - beginOffset, L"Loop End #2");
		}
		else {
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop End #2", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);
		}

		bool loopAgain;
		if (times == 0) {
			// infinite loop
			loopAgain = true;
		}
		else {
			loopCount2++;
			loopAgain = (loopCount2 != times);
		}

		if (loopAgain) {
			curOffset = loopReturnAddr2;
			loopVolumeDelta2 += volumeDelta;
			loopPitchDelta2 += pitchDelta;

			assert(loopReturnAddr2 != 0);
		}
		else {
			loopCount2 = 0;
			loopVolumeDelta2 = 0;
			loopPitchDelta2 = 0;
		}
		break;
	}

	case EVENT_TEMPO:
	{
		// actual Konami engine has tempo for each tracks,
		// here we set the song speed as a global tempo
		uint8_t newTempo = GetByte(curOffset++);
		parentSeq->tempo = newTempo;
		AddTempoBPM(beginOffset, curOffset-beginOffset, parentSeq->GetTempoInBPM());
		break;
	}

	case EVENT_TEMPO_FADE:
	{
		uint8_t newTempo = GetByte(curOffset++);
		uint8_t fadeSpeed = GetByte(curOffset++);
		desc << L"BPM: " << parentSeq->GetTempoInBPM(newTempo) << L"  Fade Length: " << (int)fadeSpeed;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tempo Fade", desc.str().c_str(), CLR_TEMPO, ICON_TEMPO);
		break;
	}

	case EVENT_TRANSPABS:
	{
		int8_t newTransp = (int8_t) GetByte(curOffset++);
		AddTranspose(beginOffset, curOffset-beginOffset, newTransp);
		break;
	}

	case EVENT_ADSR1:
	{
		uint8_t newADSR1 = GetByte(curOffset++);
		desc << L"ADSR(1): $" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)newADSR1;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR(1)", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_ADSR2:
	{
		uint8_t newADSR2 = GetByte(curOffset++);
		desc << L"ADSR(2): $" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)newADSR2;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR(2)", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_VOLUME:
	{
		uint8_t newVolume = GetByte(curOffset++);
		uint8_t midiVolume = ConvertPercentAmpToStdMidiVal(newVolume / 255.0);
		AddVol(beginOffset, curOffset-beginOffset, midiVolume);
		break;
	}

	case EVENT_VOLUME_FADE:
	{
		uint8_t newVolume = GetByte(curOffset++);
		uint8_t fadeSpeed = GetByte(curOffset++);
		desc << L"Volume: " << (int)newVolume << L"  Fade Length: " << (int)fadeSpeed;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Volume Fade", desc.str().c_str(), CLR_VOLUME, ICON_CONTROL);
		break;
	}

	case EVENT_PORTAMENTO:
	{
		uint8_t portamentoSpeed = GetByte(curOffset++);
		desc << L"Portamento Speed: " << (int)portamentoSpeed;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Portamento", desc.str().c_str(), CLR_PORTAMENTO, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_ENVELOPE_V1:
	{
		uint8_t pitchEnvDelay = GetByte(curOffset++);
		uint8_t pitchEnvSpeed = GetByte(curOffset++);
		uint8_t pitchEnvDepth = GetByte(curOffset++);
		desc << L"Delay: " << (int)pitchEnvDelay << L"  Speed: " << (int)pitchEnvSpeed << L"  Depth: " << (int)-pitchEnvDepth;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Envelope", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_ENVELOPE_V2:
	{
		uint8_t pitchEnvDelay = GetByte(curOffset++);
		uint8_t pitchEnvLength = GetByte(curOffset++);
		uint8_t pitchEnvOffset = GetByte(curOffset++);
		int16_t pitchDelta = GetShort(curOffset); curOffset += 2;
		desc << L"Delay: " << (int)pitchEnvDelay << L"  Length: " << (int)pitchEnvLength << L"  Offset: " << (int)-pitchEnvOffset << L" semitones" << L"  Delta: " << (pitchDelta / 256.0) << L" semitones";
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Envelope", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_TUNING:
	{
		int8_t newTuning = (int8_t) GetByte(curOffset++);
		double cents = GetTuningInSemitones(newTuning) * 100.0;
		AddFineTuning(beginOffset, curOffset-beginOffset, cents);
		break;
	}

	case EVENT_PITCH_SLIDE_V1:
	{
		uint8_t arg1 = GetByte(curOffset++);
		uint8_t arg2 = GetByte(curOffset++);
		uint8_t arg3 = GetByte(curOffset++);
		desc << L"Arg1: " << (int)arg1 << L"  Arg2: " << (int)arg2 << L"  Arg3: " << (int)arg3;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Slide", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_SLIDE_V2:
	{
		uint8_t arg1 = GetByte(curOffset++);
		uint8_t arg2 = GetByte(curOffset++);
		uint8_t arg3 = GetByte(curOffset++);
		desc << L"Arg1: " << (int)arg1 << L"  Arg2: " << (int)arg2 << L"  Arg3: " << (int)arg3;

		if (arg2 != 0) {
			uint8_t arg4 = GetByte(curOffset++);
			uint8_t arg5 = GetByte(curOffset++);
			uint8_t arg6 = GetByte(curOffset++);
			desc << L"Arg4: " << (int)arg4 << L"  Arg5: " << (int)arg5 << L"  Arg6: " << (int)arg6;
		}

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Slide", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_SLIDE_V3:
	{
		uint8_t pitchSlideDelay = GetByte(curOffset++);
		uint8_t pitchSlideLength = GetByte(curOffset++);
		uint8_t pitchSlideNote = GetByte(curOffset++);
		int16_t pitchDelta = GetShort(curOffset); curOffset += 2;

		uint8_t pitchSlideNoteNumber = (pitchSlideNote & 0x7f) + transpose;

		desc << L"Delay: " << (int)pitchSlideDelay << L"  Length: " << (int)pitchSlideLength << L"  Final Note: " << (int)pitchSlideNoteNumber << L"  Delta: " << (pitchDelta / 256.0) << L" semitones";
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO:
	{
		uint8_t echoChannels = GetByte(curOffset++);
		uint8_t echoVolumeL = GetByte(curOffset++);
		uint8_t echoVolumeR = GetByte(curOffset++);

		desc << L"EON: " << (int)echoChannels << L"  EVOL(L): " << (int)echoVolumeL << L"  EVOL(R): " << (int)echoVolumeR;

		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_PARAM:
	{
		uint8_t echoDelay = GetByte(curOffset++);
		uint8_t echoFeedback = GetByte(curOffset++);
		uint8_t echoArg3 = GetByte(curOffset++);

		desc << L"EDL: " << (int)echoDelay << L"  EFB: " << (int)echoFeedback << L"  Arg3: " << (int)echoArg3;

		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo Param", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_LOOP_WITH_VOLTA_START:
	{
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop With Volta Start", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

		voltaLoopStart = curOffset;
		voltaEndMeansPlayFromStart = false;
		voltaEndMeansPlayNextVolta = false;
		break;
	}

	case EVENT_LOOP_WITH_VOLTA_END:
	{
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop With Volta End", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);
		
		if (voltaEndMeansPlayFromStart)
		{
			// second time - end of first volta bracket: play from start
			voltaEndMeansPlayFromStart = false;
			voltaEndMeansPlayNextVolta = true;
			voltaLoopEnd = curOffset;
			curOffset = voltaLoopStart;
		}
		else if (voltaEndMeansPlayNextVolta)
		{
			// third time - start of first volta bracket: play the new bracket (or quit from the entire loop)
			voltaEndMeansPlayFromStart = true;
			voltaEndMeansPlayNextVolta = false;
			curOffset = voltaLoopEnd;
		}
		else
		{
			// first time - start of first volta bracket: just play it
			voltaEndMeansPlayFromStart = true;
		}
		break;
	}

	case EVENT_PAN_FADE:
	{
		uint8_t newPan = GetByte(curOffset++);
		uint8_t fadeSpeed = GetByte(curOffset++);
		desc << L"Pan: " << (int)newPan << L"  Fade Length: " << (int)fadeSpeed;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pan Fade", desc.str().c_str(), CLR_PAN, ICON_CONTROL);
		break;
	}

	case EVENT_VIBRATO_FADE:
	{
		uint8_t fadeSpeed = GetByte(curOffset++);
		desc << L"Fade Length: " << (int)fadeSpeed;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato Fade", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_ADSR_GAIN:
	{
		uint8_t newADSR1 = GetByte(curOffset++);
		uint8_t newADSR2 = GetByte(curOffset++);
		uint8_t newGAINAmount = GetByte(curOffset++);
		uint8_t newGAIN = ConvertGAINAmountToGAIN(newGAINAmount);

		desc << L"ADSR(1): $" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)newADSR1 << L"  ADSR(2): $" << (int)newADSR2 << L"  GAIN: $" << (int)newGAIN;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR(2)", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_GOTO:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		uint32_t length = curOffset - beginOffset;

		assert(dest >= dwOffset);

		if (curOffset < 0x10000 && GetByte(curOffset) == 0xff) {
			AddGenericEvent(curOffset, 1, L"End of Track", L"", CLR_TRACKEND, ICON_TRACKEND);
		}

		curOffset = dest;
		if (!IsOffsetUsed(dest)) {
			AddGenericEvent(beginOffset, length, L"Jump", desc.str().c_str(), CLR_LOOPFOREVER);
		}
		else {
			bContinue = AddLoopForever(beginOffset, length, L"Jump");
		}
		break;
	}

	case EVENT_CALL:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;

		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pattern Play", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

		assert(dest >= dwOffset);

		subReturnAddr = curOffset;
		inSubroutine = true;
		curOffset = dest;
		break;
	}

	case EVENT_END:
	{
		if (inSubroutine)
		{
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"End Pattern", desc.str().c_str(), CLR_TRACKEND, ICON_ENDREP);

			inSubroutine = false;
			curOffset = subReturnAddr;
		}
		else
		{
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			bContinue = false;
		}
		break;
	}

	default:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"KonamiSnesSeq")));
		bContinue = false;
		break;
	}

	//std::wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}
