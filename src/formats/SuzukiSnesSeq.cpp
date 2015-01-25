#include "stdafx.h"
#include "SuzukiSnesSeq.h"
#include "SuzukiSnesFormat.h"

const uint8_t durtbl[14] = { 0xBF, 0x8F, 0x5F, 0x47, 0x2F, 0x23, 0x1F, 0x17, 0x0F, 0x0B, 0x07, 0x05, 0x02, 0x00 };


DECLARE_FORMAT(SuzukiSnes);

//  *************
//  SuzukiSnesSeq
//  *************
#define MAX_TRACKS  8
#define SEQ_PPQN    96

SuzukiSnesSeq::SuzukiSnesSeq(RawFile* file, SuzukiSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
	: VGMSeq(SuzukiSnesFormat::name, file, seqdataOffset, 0, newName),
	version(ver)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap(this);
}

SuzukiSnesSeq::~SuzukiSnesSeq(void)
{
}

void SuzukiSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();
}

bool SuzukiSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* header = AddHeader(dwOffset, 0);
	uint32_t curOffset = dwOffset;

	// skip unknown stream
	if (version != SUZUKISNES_V1) {
		while (true)
		{
			if (curOffset + 1 >= 0x10000) {
				return false;
			}

			uint8_t firstByte = GetByte(curOffset);
			if (firstByte >= 0x80) {
				header->AddSimpleItem(curOffset, 1, L"Unknown Items End");
				curOffset++;
				break;
			}
			else {
				header->AddUnknownItem(curOffset, 5);
				curOffset += 5;
			}
		}
	}

	// create tracks
	for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++)
	{
		uint16_t addrTrackStart = GetShort(curOffset);

		std::wstringstream trackName;
		trackName << L"Track Pointer " << (trackIndex + 1);
		header->AddSimpleItem(curOffset, 2, trackName.str().c_str());

		aTracks.push_back(new SuzukiSnesTrack(this, addrTrackStart));
		curOffset += 2;
	}

	header->SetGuessedLength();

	return true;		//successful
}

bool SuzukiSnesSeq::GetTrackPointers(void)
{
	return true;
}

void SuzukiSnesSeq::LoadEventMap(SuzukiSnesSeq *pSeqFile)
{
	for (unsigned int statusByte = 0x00; statusByte <= 0xc3; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_NOTE;
	}

	pSeqFile->EventMap[0xc4] = EVENT_OCTAVE_UP;
	pSeqFile->EventMap[0xc5] = EVENT_OCTAVE_DOWN;
	pSeqFile->EventMap[0xc6] = EVENT_OCTAVE;
	pSeqFile->EventMap[0xc7] = EVENT_NOP;
	pSeqFile->EventMap[0xc8] = EVENT_NOISE_FREQ;
	pSeqFile->EventMap[0xc9] = EVENT_NOISE_ON;
	pSeqFile->EventMap[0xca] = EVENT_NOISE_OFF;
	pSeqFile->EventMap[0xcb] = EVENT_PITCH_MOD_ON;
	pSeqFile->EventMap[0xcc] = EVENT_PITCH_MOD_OFF;
	pSeqFile->EventMap[0xcd] = EVENT_JUMP_TO_SFX_LO;
	pSeqFile->EventMap[0xce] = EVENT_JUMP_TO_SFX_HI;
	pSeqFile->EventMap[0xcf] = EVENT_TUNING;
	pSeqFile->EventMap[0xd0] = EVENT_END;
	pSeqFile->EventMap[0xd1] = EVENT_TEMPO;
	if (version == SUZUKISNES_V1) {
		pSeqFile->EventMap[0xd2] = EVENT_LOOP_START; // duplicated
		pSeqFile->EventMap[0xd3] = EVENT_LOOP_START; // duplicated
	}
	else {
		pSeqFile->EventMap[0xd2] = EVENT_TIMER1_FREQ;
		pSeqFile->EventMap[0xd3] = EVENT_TIMER1_FREQ_REL;
	}
	pSeqFile->EventMap[0xd4] = EVENT_LOOP_START;
	pSeqFile->EventMap[0xd5] = EVENT_LOOP_END;
	pSeqFile->EventMap[0xd6] = EVENT_LOOP_BREAK;
	pSeqFile->EventMap[0xd7] = EVENT_LOOP_POINT;
	pSeqFile->EventMap[0xd8] = EVENT_ADSR_DEFAULT;
	pSeqFile->EventMap[0xd9] = EVENT_ADSR_AR;
	pSeqFile->EventMap[0xda] = EVENT_ADSR_DR;
	pSeqFile->EventMap[0xdb] = EVENT_ADSR_SL;
	pSeqFile->EventMap[0xdc] = EVENT_ADSR_SR;
	pSeqFile->EventMap[0xdd] = EVENT_DURATION_RATE;
	pSeqFile->EventMap[0xde] = EVENT_PROGCHANGE;
	pSeqFile->EventMap[0xdf] = EVENT_NOISE_FREQ_REL;
	pSeqFile->EventMap[0xe0] = EVENT_VOLUME;
	//pSeqFile->EventMap[0xe1] = 0;
	pSeqFile->EventMap[0xe2] = EVENT_VOLUME; // DUPLICATED
	pSeqFile->EventMap[0xe3] = EVENT_VOLUME_REL;
	pSeqFile->EventMap[0xe4] = EVENT_VOLUME_FADE;
	pSeqFile->EventMap[0xe5] = EVENT_PORTAMENTO;
	pSeqFile->EventMap[0xe6] = EVENT_PORTAMENTO_TOGGLE;
	pSeqFile->EventMap[0xe7] = EVENT_PAN;
	pSeqFile->EventMap[0xe8] = EVENT_PAN_FADE;
	pSeqFile->EventMap[0xe9] = EVENT_PAN_LFO_ON;
	pSeqFile->EventMap[0xea] = EVENT_PAN_LFO_RESTART;
	pSeqFile->EventMap[0xeb] = EVENT_PAN_LFO_OFF;
	pSeqFile->EventMap[0xec] = EVENT_TRANSPOSE_ABS;
	pSeqFile->EventMap[0xed] = EVENT_TRANSPOSE_REL;
	pSeqFile->EventMap[0xee] = EVENT_PERC_ON;
	pSeqFile->EventMap[0xef] = EVENT_PERC_OFF;
	pSeqFile->EventMap[0xf0] = EVENT_VIBRATO_ON;
	pSeqFile->EventMap[0xf1] = EVENT_VIBRATO_ON_WITH_DELAY;
	pSeqFile->EventMap[0xf2] = EVENT_TEMPO_REL;
	pSeqFile->EventMap[0xf3] = EVENT_VIBRATO_OFF;
	pSeqFile->EventMap[0xf4] = EVENT_TREMOLO_ON;
	pSeqFile->EventMap[0xf5] = EVENT_TREMOLO_ON_WITH_DELAY;
	if (version == SUZUKISNES_V1) {
		pSeqFile->EventMap[0xf6] = EVENT_OCTAVE_UP; // duplicated
	}
	else {
		pSeqFile->EventMap[0xf6] = EVENT_UNKNOWN1;
	}
	pSeqFile->EventMap[0xf7] = EVENT_TREMOLO_OFF;
	pSeqFile->EventMap[0xf8] = EVENT_SLUR_ON;
	pSeqFile->EventMap[0xf9] = EVENT_SLUR_OFF;
	pSeqFile->EventMap[0xfa] = EVENT_ECHO_ON;
	pSeqFile->EventMap[0xfb] = EVENT_ECHO_OFF;
	if (version == SUZUKISNES_V1) {
		pSeqFile->EventMap[0xfc] = EVENT_CALL_SFX_LO;
		pSeqFile->EventMap[0xfd] = EVENT_CALL_SFX_HI;
		pSeqFile->EventMap[0xfe] = EVENT_OCTAVE_UP; // duplicated
		pSeqFile->EventMap[0xff] = EVENT_OCTAVE_UP; // duplicated
	}
	else {
		// TODO: EventMap[0xfe..0xff] - Bahamut Lagoon, Super Mario RPG
	}
}

//  ***************
//  SuzukiSnesTrack
//  ***************

SuzukiSnesTrack::SuzukiSnesTrack(SuzukiSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
}

void SuzukiSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	vel = 100;
	vol = 0;
	octave = 4;
	/*	permode = 0;
	perkey = 0;
	transpose = 0;
	util_dur = 0;
	slurring = 0;
	rolling = 0;
	portdist = 0;
	oldpwheel = 0;
	portdtime = -1;
	pwheelrange = 0;
	loop = 0;*/
	rpt = 0;
}

bool SuzukiSnesTrack::ReadEvent(void)
{
	SuzukiSnesSeq* parentSeq = (SuzukiSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	SuzukiSnesSeqEventType eventType = (SuzukiSnesSeqEventType)0;
	std::map<uint8_t, SuzukiSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

	case EVENT_NOTE: // 0x00..0xc3
	{
		uint8_t dur = statusByte / 14;
		uint8_t note = statusByte % 14;

		if (dur == 13)
			dur = GetByte(curOffset++);
		else
			dur = durtbl[dur];
		dur = (dur + 1) << 1;

		if (note < 12)
		{
			uint8_t notedur = dur;
			uint8_t realNote = 12 * (octave)+note;		//NEEDS MODIFYING
			//if (percmode)
			//{

			//}

			//if (perckey)
			//{

			//}

			//some portamento crap goes here
			AddNoteByDur(beginOffset, curOffset - beginOffset, realNote, vel, notedur);
		}
		else if (note == 13)			//tie
		{
			MakePrevDurNoteEnd();
			AddUnknown(beginOffset, curOffset - beginOffset, L"Tie");
		}
		else							//rest
		{
			AddUnknown(beginOffset, curOffset - beginOffset, L"Rest");
		}

		AddTime(dur);
		break;
	}

	case EVENT_TEMPO:
	{
		uint8_t tempo = GetByte(curOffset++);
		AddTempo(beginOffset, curOffset - beginOffset, tempo * 125 * 48);
		break;
	}

	case EVENT_VOLUME:
	{
		uint8_t vol = GetByte(curOffset++);
		AddVol(beginOffset, curOffset - beginOffset, vol);
		break;
	}

	case EVENT_VOLUME_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		uint8_t vol = GetByte(curOffset++);
		desc << L"Fade Length: " << (int)fadeLength << L"  Volume: " << (int)vol;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Volume Fade", desc.str().c_str(), CLR_VOLUME, ICON_CONTROL);
		break;
	}

	case EVENT_PAN:
	{
		uint8_t pan = GetByte(curOffset++);
		AddPan(beginOffset, curOffset - beginOffset, pan >> 1);
		break;
	}

	case EVENT_PAN_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		uint8_t pan = GetByte(curOffset++);
		desc << L"Fade Length: " << (int)fadeLength << L"  Pan: " << (int)(pan >> 1);
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan Fade", desc.str().c_str(), CLR_PAN, ICON_CONTROL);
		break;
	}

	case EVENT_PROGCHANGE:
	{
		uint8_t newProg = GetByte(curOffset++);
		AddProgramChange(beginOffset, curOffset - beginOffset, newProg);
		break;
	}

	case EVENT_OCTAVE:
	{
		uint8_t newOctave = GetByte(curOffset++);
		AddSetOctave(beginOffset, curOffset - beginOffset, newOctave);
		break;
	}

	case EVENT_OCTAVE_UP:
	{
		AddIncrementOctave(beginOffset, curOffset - beginOffset);
		break;
	}

	case EVENT_OCTAVE_DOWN:
	{
		AddDecrementOctave(beginOffset, curOffset - beginOffset);
		break;
	}

	case EVENT_SLUR_ON:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Slur On", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_SLUR_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Slur Off", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_PERC_ON:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Percussion On", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_PERC_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Percussion Off", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_PORTAMENTO:
	{
		uint8_t arg1 = GetByte(curOffset++);
		uint8_t arg2 = GetByte(curOffset++);
		desc << L"Arg1: " << (int)arg1 << L"  Arg2: " << (int)arg2;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Portamento", desc.str().c_str(), CLR_PAN, ICON_CONTROL);
		break;
	}

	case EVENT_LOOP_START:
	{
		rptcnt[++rpt] = GetByte(curOffset++);
		rptpos[rpt] = curOffset;
		rptcur[rpt] = 0;
		//curOffset++;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Begin Repeat", desc.str().c_str(), CLR_LOOP);
		break;
	}

	case EVENT_LOOP_END:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"End Repeat", desc.str().c_str(), CLR_LOOP);
		if (--rptcnt[rpt] <= 0) {
			rpt--;
		}
		else {
			curOffset = rptpos[rpt];
			octave = rptoct[rpt];
		}
		break;
	}

	case EVENT_LOOP_POINT:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Mark Return Point", desc.str().c_str(), CLR_LOOP);
		break;
	}

	case EVENT_END:
	{
		AddEndOfTrack(beginOffset, curOffset - beginOffset);
		bContinue = false;
	}

	case EVENT_ECHO_ON:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo On", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Off", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	default:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"CompileSnesSeq")));
		bContinue = false;
		break;
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}
