#include "stdafx.h"
#include "NinSnesSeq.h"
#include "NinSnesFormat.h"
#include "SeqEvent.h"
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

	spcPercussionBase = 0;
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
		header->AddSimpleItem(beginOffset, curOffset - beginOffset, L"Playlist Jump");
		if (infiniteLoop) {
			bContinue = AddLoopForeverNoItem();
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

	switch (version) {
	case NINSNES_EARLIER:
		STATUS_NOTE_MIN = 0x80;
		STATUS_NOTE_MAX = 0xc5;
		STATUS_PERCUSSION_NOTE_MIN = 0xd0;
		STATUS_PERCUSSION_NOTE_MAX = 0xd9;
		break;

	default:
		STATUS_NOTE_MIN = 0x80;
		STATUS_NOTE_MAX = 0xc7;
		STATUS_PERCUSSION_NOTE_MIN = 0xca;
		STATUS_PERCUSSION_NOTE_MAX = 0xdf;
	}

	pSeqFile->EventMap[0x00] = EVENT_END;

	for (statusByte = 0x01; statusByte < STATUS_NOTE_MIN; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_NOTE_PARAM;
	}

	for (statusByte = STATUS_NOTE_MIN; statusByte <= STATUS_NOTE_MAX; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_NOTE;
	}

	pSeqFile->EventMap[STATUS_NOTE_MAX + 1] = EVENT_TIE;
	pSeqFile->EventMap[STATUS_NOTE_MAX + 2] = EVENT_REST;

	for (statusByte = STATUS_PERCUSSION_NOTE_MIN; statusByte <= STATUS_PERCUSSION_NOTE_MAX; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_PERCUSSION_NOTE;
	}

	switch (version) {
	case NINSNES_EARLIER:
		pSeqFile->EventMap[0xda] = EVENT_PROGCHANGE;
		pSeqFile->EventMap[0xdb] = EVENT_PAN;
		pSeqFile->EventMap[0xdc] = EVENT_PAN_FADE;
		pSeqFile->EventMap[0xdd] = EVENT_PITCH_SLIDE;
		pSeqFile->EventMap[0xde] = EVENT_VIBRATO_ON;
		pSeqFile->EventMap[0xdf] = EVENT_VIBRATO_OFF;
		pSeqFile->EventMap[0xe0] = EVENT_MASTER_VOLUME;
		pSeqFile->EventMap[0xe1] = EVENT_MASTER_VOLUME_FADE;
		pSeqFile->EventMap[0xe2] = EVENT_TEMPO;
		pSeqFile->EventMap[0xe3] = EVENT_TEMPO_FADE;
		pSeqFile->EventMap[0xe4] = EVENT_GLOBAL_TRANSPOSE;
		pSeqFile->EventMap[0xe5] = EVENT_TREMOLO_ON;
		pSeqFile->EventMap[0xe6] = EVENT_TREMOLO_OFF;
		pSeqFile->EventMap[0xe7] = EVENT_VOLUME;
		pSeqFile->EventMap[0xe8] = EVENT_VOLUME_FADE;
		pSeqFile->EventMap[0xe9] = EVENT_CALL;
		pSeqFile->EventMap[0xea] = EVENT_VIBRATO_FADE;
		pSeqFile->EventMap[0xeb] = EVENT_PITCH_ENVELOPE_TO;
		pSeqFile->EventMap[0xec] = EVENT_PITCH_ENVELOPE_FROM;
		//pSeqFile->EventMap[0xed] = EVENT_PITCH_ENVELOPE_OFF;
		pSeqFile->EventMap[0xee] = EVENT_TUNING;
		pSeqFile->EventMap[0xef] = EVENT_ECHO_ON;
		pSeqFile->EventMap[0xf0] = EVENT_ECHO_OFF;
		pSeqFile->EventMap[0xf1] = EVENT_ECHO_PARAM;
		pSeqFile->EventMap[0xf2] = EVENT_ECHO_VOLUME_FADE;

		if (volumeTable.empty()) {
			const uint8_t ninVolumeTableEarlier[16] = {
				0x08, 0x12, 0x1b, 0x24, 0x2c, 0x35, 0x3e, 0x47,
				0x51, 0x5a, 0x62, 0x6b, 0x7d, 0x8f, 0xa1, 0xb3,
			};
			volumeTable.assign(std::begin(ninVolumeTableEarlier), std::end(ninVolumeTableEarlier));
		}

		if (durRateTable.empty()) {
			const uint8_t ninDurRateTableEarlier[8] = {
				0x33, 0x66, 0x80, 0x99, 0xb3, 0xcc, 0xe6, 0xff,
			};
			durRateTable.assign(std::begin(ninDurRateTableEarlier), std::end(ninDurRateTableEarlier));
		}

		if (panTable.empty()) {
			const uint8_t ninpanTableEarlier[21] = {
				0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
				0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
				0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
			};
			panTable.assign(std::begin(ninpanTableEarlier), std::end(ninpanTableEarlier));
		}
		break;

	default:
		pSeqFile->EventMap[0xe0] = EVENT_PROGCHANGE;
		pSeqFile->EventMap[0xe1] = EVENT_PAN;
		pSeqFile->EventMap[0xe2] = EVENT_PAN_FADE;
		pSeqFile->EventMap[0xe3] = EVENT_VIBRATO_ON;
		pSeqFile->EventMap[0xe4] = EVENT_VIBRATO_OFF;
		pSeqFile->EventMap[0xe5] = EVENT_MASTER_VOLUME;
		pSeqFile->EventMap[0xe6] = EVENT_MASTER_VOLUME_FADE;
		pSeqFile->EventMap[0xe7] = EVENT_TEMPO;
		pSeqFile->EventMap[0xe8] = EVENT_TEMPO_FADE;
		pSeqFile->EventMap[0xe9] = EVENT_GLOBAL_TRANSPOSE;
		pSeqFile->EventMap[0xea] = EVENT_TRANSPOSE;
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

		if (volumeTable.empty()) {
			const uint8_t ninVolumeTableStandard[16] = {
				0x4c, 0x59, 0x6d, 0x7f, 0x87, 0x8e, 0x98, 0xa0,
				0xa8, 0xb2, 0xbf, 0xcb, 0xd8, 0xe5, 0xf2, 0xfc,
			};
			volumeTable.assign(std::begin(ninVolumeTableStandard), std::end(ninVolumeTableStandard));
		}

		if (durRateTable.empty()) {
			const uint8_t ninDurRateTableStandard[8] = {
				0x65, 0x7f, 0x98, 0xb2, 0xcb, 0xe5, 0xf2, 0xfc,
			};
			durRateTable.assign(std::begin(ninDurRateTableStandard), std::end(ninDurRateTableStandard));
		}

		if (panTable.empty()) {
			const uint8_t ninpanTableStandard[21] = {
				0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
				0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
				0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
			};
			panTable.assign(std::begin(ninpanTableStandard), std::end(ninpanTableStandard));
		}
	}
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
			NinSnesTrack* track = new NinSnesTrack(this, curOffset, 2, L"NULL");
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
	spcNoteVolume(0xfc),
	lastNoteNumberForTie(0)
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
	if (!available) {
		AddTime(1);
		return true;
	}

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
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Section End", desc.str().c_str(), CLR_TRACKEND, ICON_TRACKEND);
			if (readMode == READMODE_FIND_DELTA_LENGTH) {
				deltaLength = GetTime();
			}

			// finish this section as soon as possible
			// TODO: cancel all expected note and fader-output events
			parentSeq->InactivateAllTracks();
			bContinue = false;
		}
		else {
			uint32_t eventLength = curOffset - beginOffset;

			loopCount--;
			if (loopCount == 0) {
				// repeat end
				curOffset = loopReturnAddress;
			}
			else {
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
		desc << L"Duration: " << (int)spcNoteDuration;

		// param #2: quantize and velocity (optional)
		if (curOffset + 1 < 0x10000) {
			uint8_t quantizeAndVelocity = GetByte(curOffset);
			if (quantizeAndVelocity <= 0x7f) {
				uint8_t durIndex = (quantizeAndVelocity >> 4) & 7;
				uint8_t velIndex = quantizeAndVelocity & 15;

				curOffset++;
				spcNoteDurRate = parentSeq->durRateTable[durIndex];
				spcNoteVolume = parentSeq->volumeTable[velIndex];

				desc << L"  Quantize: " << (int)durIndex << L" (" << (int)spcNoteDurRate << L"/256)" << L"  Velocity: " << (int)velIndex << L" (" << (int)spcNoteVolume << L"/256)";
			}
		}

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Note Param", desc.str().c_str(), CLR_DURNOTE, ICON_CONTROL);
		break;
	}

	case EVENT_NOTE:
	{
		uint8_t noteNumber = statusByte - parentSeq->STATUS_NOTE_MIN;
		uint8_t duration = max((spcNoteDuration * spcNoteDurRate) >> 8, 1);

		lastNoteNumberForTie = noteNumber;
		AddNoteByDur(beginOffset, curOffset - beginOffset, noteNumber, spcNoteVolume / 2, duration, L"Note");
		AddTime(spcNoteDuration);
		break;
	}

	case EVENT_TIE:
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str().c_str(), CLR_TIE);
		AddNoteByDurNoItem(lastNoteNumberForTie, spcNoteVolume / 2, spcNoteDuration);
		AddTime(spcNoteDuration);
		break;

	case EVENT_REST:
		AddRest(beginOffset, curOffset - beginOffset, spcNoteDuration);
		break;

	case EVENT_PERCUSSION_NOTE:
	{
		uint8_t noteNumber = statusByte - parentSeq->STATUS_NOTE_MIN;
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

		uint8_t panIndex = (uint8_t)min((unsigned)(newPan & 0x1f), parentSeq->panTable.size() - 1);
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

		uint8_t panIndex = (uint8_t)min((unsigned)(newPan & 0x1f), parentSeq->panTable.size() - 1);

		uint8_t spcPan = parentSeq->panTable[panIndex];
		uint8_t midiPan = Convert7bitPercentPanValToStdMidiVal(spcPan);

		// TODO: fade in real curve
		AddPanSlide(beginOffset, curOffset - beginOffset, fadeLength, midiPan);
		break;
	}

	case EVENT_VIBRATO_ON:
	{
		uint8_t vibratoDelay = GetByte(curOffset++);
		uint8_t vibratoRate = GetByte(curOffset++);
		uint8_t vibratoDepth = GetByte(curOffset++);

		desc << L"Delay: " << (int)vibratoDelay << L"  Rate: " << (int)vibratoRate << L"  Depth: " << (int)vibratoDepth;
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

		desc << L"Length: " << (int)fadeLength << L"  Volume: " << (int)newVol;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Master Volume Fade", desc.str().c_str(), CLR_VOLUME, ICON_CONTROL);
		break;
	}

	case EVENT_TEMPO:
	{
		uint8_t newTempo = GetByte(curOffset++);
		AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
		break;
	}

	case EVENT_TEMPO_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		uint8_t newTempo = GetByte(curOffset++);

		AddTempoSlide(beginOffset, curOffset - beginOffset, fadeLength, (int)(60000000 / parentSeq->GetTempoInBPM(newTempo)));
		break;
	}

	case EVENT_GLOBAL_TRANSPOSE:
	{
		int8_t semitones = GetByte(curOffset++);
		AddGlobalTranspose(beginOffset, curOffset - beginOffset, semitones);
		break;
	}

	case EVENT_TRANSPOSE:
	{
		int8_t semitones = GetByte(curOffset++);
		AddTranspose(beginOffset, curOffset - beginOffset, semitones);
		break;
	}

	case EVENT_TREMOLO_ON:
	{
		uint8_t tremoloDelay = GetByte(curOffset++);
		uint8_t tremoloRate = GetByte(curOffset++);
		uint8_t tremoloDepth = GetByte(curOffset++);

		desc << L"Delay: " << (int)tremoloDelay << L"  Rate: " << (int)tremoloRate << L"  Depth: " << (int)tremoloDepth;
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
		uint8_t times = GetByte(curOffset++);

		loopReturnAddress = curOffset;
		loopStartAddress = dest;
		loopCount = times;

		desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest
			<< std::dec << std::setfill(L' ') << std::setw(0) << L"  Times: " << (int)times;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pattern Play", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

		curOffset = loopStartAddress;
		break;
	}

	case EVENT_VIBRATO_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		desc << L"Length: " << (int)fadeLength;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Fade", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_ENVELOPE_TO:
	{
		uint8_t pitchEnvDelay = GetByte(curOffset++);
		uint8_t pitchEnvLength = GetByte(curOffset++);
		int8_t pitchEnvSemitones = (int8_t)GetByte(curOffset++);

		desc << L"Delay: " << (int)pitchEnvDelay << L"  Length: " << (int)pitchEnvLength << L"  Semitones: " << (int)pitchEnvSemitones;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Envelope (To)", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_ENVELOPE_FROM:
	{
		uint8_t pitchEnvDelay = GetByte(curOffset++);
		uint8_t pitchEnvLength = GetByte(curOffset++);
		int8_t pitchEnvSemitones = (int8_t)GetByte(curOffset++);

		desc << L"Delay: " << (int)pitchEnvDelay << L"  Length: " << (int)pitchEnvLength << L"  Semitones: " << (int)pitchEnvSemitones;
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

		desc << L"Channels: ";
		for (int channelNo = MAX_TRACKS - 1; channelNo >= 0; channelNo--) {
			if ((spcEON & (1 << channelNo)) != 0) {
				desc << (int)channelNo;
			}
			else {
				desc << L"-";
			}
		}

		desc << L"  Volume Left: " << (int)spcEVOL_L << L"  Volume Right: " << (int)spcEVOL_R;
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

		// TODO: dump actual FIR filter values

		desc << L"Delay: " << (int)spcEDL << L"  Feedback: " << (int)spcEFB << L"  FIR: " << (int)spcFIR;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Off", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_VOLUME_FADE:
	{
		uint8_t fadeLength = GetByte(curOffset++);
		uint8_t spcEVOL_L = GetByte(curOffset++);
		uint8_t spcEVOL_R = GetByte(curOffset++);

		desc << L"Length: " << (int)fadeLength << L"  Volume Left: " << (int)spcEVOL_L << L"  Volume Right: " << (int)spcEVOL_R;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Volume Fade", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_SLIDE:
	{
		uint8_t pitchSlideDelay = GetByte(curOffset++);
		uint8_t pitchSlideLength = GetByte(curOffset++);
		uint8_t pitchSlideTargetNote = GetByte(curOffset++);

		desc << L"Delay: " << (int)pitchSlideDelay << L"  Length: " << (int)pitchSlideLength << L"  Note: " << (int)(pitchSlideTargetNote - 0x80);
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Slide", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_PERCCUSION_PATCH_BASE:
	{
		uint8_t percussionBase = GetByte(curOffset++);
		parentSeq->spcPercussionBase = percussionBase;

		desc << L"Percussion Base: " << (int)percussionBase;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Percussion Base", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}
	}

	return bContinue;
}
