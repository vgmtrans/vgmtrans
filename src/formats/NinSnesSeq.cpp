#include "stdafx.h"
#include "NinSnesSeq.h"
#include "NinSnesFormat.h"
#include "SeqEvent.h"
#include "Options.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(NinSnes);

//  **********
//  NinSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    48
#define SEQ_KEYOFS  24

NinSnesSeq::NinSnesSeq(RawFile* file, NinSnesVersion ver, uint32_t offset, std::wstring theName)
	: VGMMultiSectionSeq(NinSnesFormat::name, file, offset, 0), version(ver),
	header(NULL)
{
	name = theName;

	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap(this);
}

NinSnesSeq::~NinSnesSeq()
{
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

bool NinSnesSeq::ReadEvent(long stopTime)
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

		if (!LoadSection(section, stopTime)) {
			bContinue = false;
		}
	}

	return bContinue;
}

void NinSnesSeq::LoadEventMap(NinSnesSeq *pSeqFile)
{
	int statusByte;

	pSeqFile->EventMap[0x00] = EVENT_END;

	for (statusByte = 0x01; statusByte <= 0x7f; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_NOTE_PARAM;
	}

	for (statusByte = 0x80; statusByte <= 0xc7; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_NOTE;
	}

	pSeqFile->EventMap[0xc8] = EVENT_TIE;
	pSeqFile->EventMap[0xc9] = EVENT_REST;

	for (statusByte = 0xca; statusByte <= 0xdf; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_PERCUSSION_NOTE;
	}

	pSeqFile->EventMap[0xe0] = EVENT_PROGCHANGE;
	pSeqFile->EventMap[0xe1] = EVENT_PAN;
	pSeqFile->EventMap[0xe2] = EVENT_PAN_FADE;
	pSeqFile->EventMap[0xe3] = EVENT_VIBRATO_ON;
	pSeqFile->EventMap[0xe4] = EVENT_VIBRATO_OFF;
	pSeqFile->EventMap[0xe5] = EVENT_MASTER_VOLUME;
	pSeqFile->EventMap[0xe6] = EVENT_MASTER_VOLUME_FADE;
	pSeqFile->EventMap[0xe7] = EVENT_TEMPO;
	pSeqFile->EventMap[0xe8] = EVENT_TEMPO_FADE;
	pSeqFile->EventMap[0xe9] = EVENT_TRANSPOSE_ABS;
	pSeqFile->EventMap[0xea] = EVENT_TRANSPOSE_REL;
	pSeqFile->EventMap[0xeb] = EVENT_TREMOLO_ON;
	pSeqFile->EventMap[0xec] = EVENT_TREMOLO_OFF;
	pSeqFile->EventMap[0xed] = EVENT_VOLUME;
	pSeqFile->EventMap[0xee] = EVENT_VOLUME_FADE;
	pSeqFile->EventMap[0xef] = EVENT_CALL;
	pSeqFile->EventMap[0xf0] = EVENT_VIBRATO_FADE;
	pSeqFile->EventMap[0xf1] = EVENT_PITCH_ENVELOPE_TO;
	pSeqFile->EventMap[0xf2] = EVENT_PITCH_ENVELOPE_FROM;
	pSeqFile->EventMap[0xf3] = EVENT_PITCH_ENVELOPE_OFF;
	pSeqFile->EventMap[0xf4] = EVENT_TUNING;
	pSeqFile->EventMap[0xf5] = EVENT_ECHO_ON;
	pSeqFile->EventMap[0xf6] = EVENT_ECHO_OFF;
	pSeqFile->EventMap[0xf7] = EVENT_ECHO_PARAM;
	pSeqFile->EventMap[0xf8] = EVENT_ECHO_VOLUME_FADE;
	pSeqFile->EventMap[0xf9] = EVENT_PITCH_SLIDE;
	pSeqFile->EventMap[0xfa] = EVENT_PERCCUSION_PATCH_BASE;

	const uint8_t ninVolumeTableStandard[16] = {
		0x4c, 0x59, 0x6d, 0x7f, 0x87, 0x8e, 0x98, 0xa0,
		0xa8, 0xb2, 0xbf, 0xcb, 0xd8, 0xe5, 0xf2, 0xfc,
	};
	volumeTable.assign(std::begin(ninVolumeTableStandard), std::end(ninVolumeTableStandard));

	const uint8_t ninDurRateTableStandard[8] = {
		0x65, 0x7f, 0x98, 0xb2, 0xcb, 0xe5, 0xf2, 0xfc,
	};
	durRateTable.assign(std::begin(ninDurRateTableStandard), std::end(ninDurRateTableStandard));

	const uint8_t ninpanTableStandard[21] = {
		0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
		0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
		0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
	};
	panTable.assign(std::begin(ninpanTableStandard), std::end(ninpanTableStandard));
}

double NinSnesSeq::GetTempoInBPM()
{
	return GetTempoInBPM(spcTempo);
}

double NinSnesSeq::GetTempoInBPM(uint8_t tempo)
{
	if (tempo != 0)
	{
		return (double)60000000 / (SEQ_PPQN * 2000) * ((double)tempo / 256);
	}
	else
	{
		return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
	}
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
	available(true),
	spcNoteDuration(1),
	spcNoteDurRate(0xfc),
	spcNoteVolume(0xfc)
{
	if (theName != NULL) {
		name = theName;
	}

	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
}

void NinSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	cKeyCorrection = SEQ_KEYOFS;

	loopCount = 0;
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

	std::wstringstream desc;

	NinSnesSeqEventType eventType = (NinSnesSeqEventType)0;
	std::map<uint8_t, NinSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
	if (pEventType != parentSeq->EventMap.end()) {
		eventType = pEventType->second;
	}

	switch (eventType)
	{
	case EVENT_UNKNOWN0:
	{
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		break;
	}

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

	case EVENT_END:
	{
		if (loopCount == 0) {
			AddEndOfTrack(beginOffset, curOffset - beginOffset);
			bContinue = false;
		}
		else {
			uint32_t eventLength = curOffset - beginOffset;

			loopCount--;
			if (loopCount != 0) {
				// repeat again
				curOffset = loopStartAddress;
			}

			AddGenericEvent(beginOffset, eventLength, L"Pattern End", desc.str().c_str(), CLR_TRACKEND, ICON_TRACKEND);
		}
		break;
	}

	case EVENT_NOTE_PARAM:
	{
		// param #1: duration
		spcNoteDuration = statusByte;

		// param #2: quantize and velocity (optional)
		if (curOffset + 1 < 0x10000) {
			uint8_t quantizeAndVelocity = GetByte(curOffset);
			if (quantizeAndVelocity <= 0x7f) {
				curOffset++;
				spcNoteDurRate = parentSeq->durRateTable[(quantizeAndVelocity >> 4) & 7];
				spcNoteVolume = parentSeq->volumeTable[quantizeAndVelocity & 15];
			}
		}

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Note Param", desc.str().c_str(), CLR_DURNOTE, ICON_CONTROL);
		break;
	}

	case EVENT_NOTE:
	{
		uint8_t noteNumber = statusByte - 0x80;
		uint8_t duration = max((spcNoteDuration * spcNoteDurRate) >> 8, 1);
		AddNoteByDur(beginOffset, curOffset - beginOffset, noteNumber, spcNoteVolume / 2, duration, L"Note");
		AddTime(spcNoteDuration);
		break;
	}

	case EVENT_TIE:
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str().c_str(), CLR_TIE);
		break;

	case EVENT_REST:
		AddRest(beginOffset, curOffset - beginOffset, spcNoteDuration);
		break;

	case EVENT_PERCUSSION_NOTE:
	{
		uint8_t noteNumber = statusByte - 0x80;
		uint8_t duration = max((spcNoteDuration * spcNoteDurRate) >> 8, 1);
		AddPercNoteByDur(beginOffset, curOffset - beginOffset, noteNumber, spcNoteVolume / 2, duration, L"Percussion Note");
		AddTime(spcNoteDuration);
		break;
	}

	case EVENT_PROGCHANGE:
	{
		uint8_t newProgNum = GetByte(curOffset++);
		AddProgramChange(beginOffset, curOffset - beginOffset, newProgNum, true);
		break;
	}

	case EVENT_PAN:
	{
		uint8_t newPan = GetByte(curOffset++);

		uint8_t panIndex = min((unsigned)(newPan & 0x1f), parentSeq->panTable.size() - 1);
		bool reverseLeft = (newPan & 0x80) != 0;
		bool reverseRight = (newPan & 0x40) != 0;

		uint8_t spcPan = parentSeq->panTable[panIndex];
		uint8_t midiPan = Convert7bitPercentPanValToStdMidiVal(spcPan);

		AddPan(beginOffset, curOffset - beginOffset, midiPan);
		break;
	}

	case EVENT_PAN_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		uint8_t newPan = GetByte(curOffset++);

		uint8_t panIndex = min((unsigned)(newPan & 0x1f), parentSeq->panTable.size() - 1);

		uint8_t spcPan = parentSeq->panTable[panIndex];
		uint8_t midiPan = Convert7bitPercentPanValToStdMidiVal(spcPan);

		AddPanSlide(beginOffset, curOffset - beginOffset, fadeLength, newPan);
	}

	case EVENT_VIBRATO_ON:
	{
		uint8_t vibratoDelay = GetByte(curOffset++);
		uint8_t vibratoRate = GetByte(curOffset++);
		uint8_t vibratoDepth = GetByte(curOffset++);

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_VIBRATO_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Off", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_MASTER_VOLUME:
	{
		uint8_t newVol = GetByte(curOffset++);
		AddMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
		break;
	}

	case EVENT_MASTER_VOLUME_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		uint8_t newVol = GetByte(curOffset++);
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Master Volume Fade", desc.str().c_str(), CLR_VOLUME, ICON_CONTROL);
		break;
	}

	case EVENT_TEMPO:
	{
		uint8_t newTempo = GetByte(curOffset++);
		parentSeq->spcTempo = newTempo;
		AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM());
		break;
	}

	case EVENT_TEMPO_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		uint8_t newTempo = GetByte(curOffset++);

		parentSeq->spcTempo = newTempo;
		AddTempoSlide(beginOffset, curOffset - beginOffset, fadeLength, (int)(60000000 / parentSeq->GetTempoInBPM(newTempo)));
		break;
	}

	case EVENT_TRANSPOSE_ABS:
	{
		curOffset++;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Transpose (Absolute)", desc.str().c_str(), CLR_TRANSPOSE, ICON_CONTROL);
		break;
	}

	case EVENT_TRANSPOSE_REL:
	{
		curOffset++;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Transpose (Relative)", desc.str().c_str(), CLR_TRANSPOSE, ICON_CONTROL);
		break;
	}

	case EVENT_TREMOLO_ON:
	{
		uint8_t tremoloDelay = GetByte(curOffset++);
		uint8_t tremoloRate = GetByte(curOffset++);
		uint8_t tremoloDepth = GetByte(curOffset++);

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_TREMOLO_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo Off", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_VOLUME:
	{
		uint8_t newVol = GetByte(curOffset++);
		AddVol(beginOffset, curOffset - beginOffset, newVol / 2);
		break;
	}

	case EVENT_VOLUME_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		uint8_t newVol = GetByte(curOffset++);
		AddVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol);
		break;
	}

	case EVENT_CALL:
	{
		uint16_t dest = GetShort(curOffset); curOffset += 2;
		uint8_t count = GetByte(curOffset++);

		loopReturnAddress = curOffset;
		loopStartAddress = dest;
		loopCount = count;

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern Play", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);
		break;
	}

	case EVENT_VIBRATO_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Fade", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_ENVELOPE_TO:
	{
		uint8_t pitchEnvDelay = GetByte(curOffset++);
		uint8_t pitchEnvLength = GetByte(curOffset++);
		int8_t pitchEnvKey = (int8_t)GetByte(curOffset++);

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Envelope (To)", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_ENVELOPE_FROM:
	{
		uint8_t pitchEnvDelay = GetByte(curOffset++);
		uint8_t pitchEnvLength = GetByte(curOffset++);
		int8_t pitchEnvKey = (int8_t)GetByte(curOffset++);

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Envelope (From)", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_ENVELOPE_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Envelope Off", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_TUNING:
	{
		uint8_t newTuning = GetByte(curOffset++);
		AddFineTuning(beginOffset, curOffset - beginOffset, newTuning / 256.0);
		break;
	}

	case EVENT_ECHO_ON:
	{
		uint8_t spcEON = GetByte(curOffset++);
		uint8_t spcEVOL_L = GetByte(curOffset++);
		uint8_t spcEVOL_R = GetByte(curOffset++);

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Off", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_PARAM:
	{
		uint8_t spcEDL = GetByte(curOffset++);
		uint8_t spcEFB = GetByte(curOffset++);
		uint8_t spcFIR = GetByte(curOffset++);

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Off", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_VOLUME_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		uint8_t spcEVOL_L = GetByte(curOffset++);
		uint8_t spcEVOL_R = GetByte(curOffset++);

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Volume Fade", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_SLIDE:
	{
		uint8_t pitchSlideDelay = GetByte(curOffset++);
		uint8_t pitchSlideLength = GetByte(curOffset++);
		uint8_t pitchSlideTargetNote = GetByte(curOffset++);

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Slide", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_PERCCUSION_PATCH_BASE:
	{
		uint8_t percBase = GetByte(curOffset++);

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Percussion Base", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}
	}

	return bContinue;
}
