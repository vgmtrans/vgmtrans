#include "stdafx.h"
#include "AkaoSnesSeq.h"
#include "AkaoSnesFormat.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(AkaoSnes);

//  **********
//  AkaoSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    48

AkaoSnesSeq::AkaoSnesSeq(RawFile* file, AkaoSnesVersion ver, AkaoSnesMinorVersion minorVer, uint32_t seqdataOffset, uint32_t addrAPURelocBase, std::wstring newName)
	: VGMSeq(AkaoSnesFormat::name, file, seqdataOffset, 0, newName),
	version(ver),
	minorVersion(minorVer),
	addrAPURelocBase(addrAPURelocBase),
	addrROMRelocBase(addrAPURelocBase),
	addrSequenceEnd(0)
{
	bLoadTickByTick = true;
	bAllowDiscontinuousTrackData = true;

	UseReverb();
	AlwaysWriteInitialReverb(0);

	LoadEventMap(this);
}

AkaoSnesSeq::~AkaoSnesSeq(void)
{
}

void AkaoSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();
}

bool AkaoSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* header = AddHeader(dwOffset, 0);
	uint32_t curOffset = dwOffset;
	if (version == AKAOSNES_V1 || version == AKAOSNES_V2) {
		// Earlier versions are not relocatable
		// All addresses are plain APU addresses
		addrROMRelocBase = addrAPURelocBase;
		addrSequenceEnd = ROMAddressToAPUAddress(0);
	}
	else {
		// Later versions are relocatable
		if (version == AKAOSNES_V3) {
			header->AddSimpleItem(curOffset, 2, L"ROM Address Base");
			addrROMRelocBase = GetShort(curOffset);
			if (minorVersion != AKAOSNES_V3_FFMQ) {
				curOffset += 2;
			}

			header->AddSimpleItem(curOffset + MAX_TRACKS * 2, 2, L"End Address");
			addrSequenceEnd = GetShortAddress(curOffset + MAX_TRACKS * 2);
		}
		else if (version == AKAOSNES_V4) {
			header->AddSimpleItem(curOffset, 2, L"ROM Address Base");
			addrROMRelocBase = GetShort(curOffset); curOffset += 2;

			header->AddSimpleItem(curOffset, 2, L"End Address");
			addrSequenceEnd = GetShortAddress(curOffset); curOffset += 2;
		}

		// calculate sequence length
		if (addrSequenceEnd < dwOffset) {
			return false;
		}
		unLength = addrSequenceEnd - dwOffset;
	}

	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		uint16_t addrTrackStart = GetShortAddress(curOffset);
		if (addrTrackStart != addrSequenceEnd) {
			std::wstringstream trackName;
			trackName << L"Track Pointer " << (trackIndex + 1);
			header->AddSimpleItem(curOffset, 2, trackName.str().c_str());
		}
		else {
			header->AddSimpleItem(curOffset, 2, L"NULL");
		}
		curOffset += 2;
	}

	header->SetGuessedLength();

	return true;		//successful
}


bool AkaoSnesSeq::GetTrackPointers(void)
{
	uint32_t curOffset = dwOffset;
	if (version == AKAOSNES_V3) {
		if (minorVersion != AKAOSNES_V3_FFMQ) {
			curOffset += 2;
		}
	}
	else if (version == AKAOSNES_V4) {
		curOffset += 4;
	}

	for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
		uint16_t addrTrackStart = GetShortAddress(curOffset);
		if (addrTrackStart != addrSequenceEnd) {
			AkaoSnesTrack* track = new AkaoSnesTrack(this, addrTrackStart);
			aTracks.push_back(track);
		}
		curOffset += 2;
	}

	return true;
}

void AkaoSnesSeq::LoadEventMap(AkaoSnesSeq *pSeqFile)
{
	int statusByte;

	if (version == AKAOSNES_V4) {
		STATUS_NOTE_MAX = 0xc3;
	}
	else {
		STATUS_NOTE_MAX = 0xd1;
	}

	for (statusByte = 0x00; statusByte <= STATUS_NOTE_MAX; statusByte++) {
		pSeqFile->EventMap[statusByte] = EVENT_NOTE;
	}

	if (version == AKAOSNES_V1) {
		pSeqFile->EventMap[0xd2] = EVENT_TEMPO_FADE;
		pSeqFile->EventMap[0xd3] = EVENT_NOP1;
		pSeqFile->EventMap[0xd4] = EVENT_ECHO_VOLUME;
		pSeqFile->EventMap[0xd5] = EVENT_ECHO_FEEDBACK_FIR;
		pSeqFile->EventMap[0xd6] = EVENT_PITCH_SLIDE_ON;
		pSeqFile->EventMap[0xd7] = EVENT_TREMOLO_ON;
		pSeqFile->EventMap[0xd8] = EVENT_VIBRATO_ON;
		pSeqFile->EventMap[0xd9] = EVENT_PAN_LFO_ON_WITH_DELAY;
		pSeqFile->EventMap[0xda] = EVENT_OCTAVE;
		pSeqFile->EventMap[0xdb] = EVENT_PROGCHANGE;
		pSeqFile->EventMap[0xdc] = EVENT_VOLUME_ENVELOPE;
		pSeqFile->EventMap[0xdd] = EVENT_GAIN_RELEASE;
		pSeqFile->EventMap[0xde] = EVENT_GAIN_SUSTAIN;
		pSeqFile->EventMap[0xdf] = EVENT_NOISE_FREQ;
		pSeqFile->EventMap[0xe0] = EVENT_LOOP_START;
		pSeqFile->EventMap[0xe1] = EVENT_OCTAVE_UP;
		pSeqFile->EventMap[0xe2] = EVENT_OCTAVE_DOWN;
		pSeqFile->EventMap[0xe3] = EVENT_NOP;
		pSeqFile->EventMap[0xe4] = EVENT_NOP;
		pSeqFile->EventMap[0xe5] = EVENT_NOP;
		pSeqFile->EventMap[0xe6] = EVENT_PITCH_SLIDE_OFF;
		pSeqFile->EventMap[0xe7] = EVENT_TREMOLO_OFF;
		pSeqFile->EventMap[0xe8] = EVENT_VIBRATO_OFF;
		pSeqFile->EventMap[0xe9] = EVENT_PAN_LFO_OFF;
		pSeqFile->EventMap[0xea] = EVENT_ECHO_ON;
		pSeqFile->EventMap[0xeb] = EVENT_ECHO_OFF;
		pSeqFile->EventMap[0xec] = EVENT_NOISE_ON;
		pSeqFile->EventMap[0xed] = EVENT_NOISE_OFF;
		pSeqFile->EventMap[0xee] = EVENT_PITCHMOD_ON;
		pSeqFile->EventMap[0xef] = EVENT_PITCHMOD_OFF;
		pSeqFile->EventMap[0xf0] = EVENT_LOOP_END;
		pSeqFile->EventMap[0xf1] = EVENT_END;
		pSeqFile->EventMap[0xf2] = EVENT_VOLUME_FADE;
		pSeqFile->EventMap[0xf3] = EVENT_PAN_FADE;
		pSeqFile->EventMap[0xf4] = EVENT_GOTO;
		pSeqFile->EventMap[0xf5] = EVENT_CONDITIONAL_JUMP;
		pSeqFile->EventMap[0xf6] = EVENT_UNKNOWN0; // cpu-controled jump?
		pSeqFile->EventMap[0xf7] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xf8] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xf9] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xfa] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xfb] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xfc] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xfd] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xfe] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xff] = EVENT_END; // duplicated
	}
	else if (version == AKAOSNES_V2) {
		pSeqFile->EventMap[0xd2] = EVENT_TEMPO;
		pSeqFile->EventMap[0xd3] = EVENT_TEMPO_FADE;
		pSeqFile->EventMap[0xd4] = EVENT_VOLUME;
		pSeqFile->EventMap[0xd5] = EVENT_VOLUME_FADE;
		pSeqFile->EventMap[0xd6] = EVENT_PAN;
		pSeqFile->EventMap[0xd7] = EVENT_PAN_FADE;
		pSeqFile->EventMap[0xd8] = EVENT_ECHO_VOLUME;
		pSeqFile->EventMap[0xd9] = EVENT_ECHO_VOLUME_FADE;
		pSeqFile->EventMap[0xda] = EVENT_TRANSPOSE_ABS;
		pSeqFile->EventMap[0xdb] = EVENT_PITCH_SLIDE_ON;
		pSeqFile->EventMap[0xdc] = EVENT_PITCH_SLIDE_OFF;
		pSeqFile->EventMap[0xdd] = EVENT_TREMOLO_ON;
		pSeqFile->EventMap[0xde] = EVENT_TREMOLO_OFF;
		pSeqFile->EventMap[0xdf] = EVENT_VIBRATO_ON;
		pSeqFile->EventMap[0xe0] = EVENT_VIBRATO_OFF;
		pSeqFile->EventMap[0xe1] = EVENT_NOISE_FREQ;
		pSeqFile->EventMap[0xe2] = EVENT_NOISE_ON;
		pSeqFile->EventMap[0xe3] = EVENT_NOISE_OFF;
		pSeqFile->EventMap[0xe4] = EVENT_PITCHMOD_ON;
		pSeqFile->EventMap[0xe5] = EVENT_PITCHMOD_OFF;
		pSeqFile->EventMap[0xe6] = EVENT_ECHO_FEEDBACK_FIR;
		pSeqFile->EventMap[0xe7] = EVENT_ECHO_ON;
		pSeqFile->EventMap[0xe8] = EVENT_ECHO_OFF;
		pSeqFile->EventMap[0xe9] = EVENT_PAN_LFO_ON;
		pSeqFile->EventMap[0xea] = EVENT_PAN_LFO_OFF;
		pSeqFile->EventMap[0xeb] = EVENT_OCTAVE;
		pSeqFile->EventMap[0xec] = EVENT_OCTAVE_UP;
		pSeqFile->EventMap[0xed] = EVENT_OCTAVE_DOWN;
		pSeqFile->EventMap[0xee] = EVENT_LOOP_START;
		pSeqFile->EventMap[0xef] = EVENT_LOOP_END;
		pSeqFile->EventMap[0xf0] = EVENT_CONDITIONAL_JUMP;
		pSeqFile->EventMap[0xf1] = EVENT_GOTO;
		pSeqFile->EventMap[0xf2] = EVENT_SLUR_ON;
		pSeqFile->EventMap[0xf3] = EVENT_PROGCHANGE;
		pSeqFile->EventMap[0xf4] = EVENT_VOLUME_ENVELOPE;
		pSeqFile->EventMap[0xf5] = EVENT_SLUR_OFF;
		pSeqFile->EventMap[0xf6] = EVENT_UNKNOWN2; // cpu-controled jump?
		pSeqFile->EventMap[0xf7] = EVENT_TUNING;
		pSeqFile->EventMap[0xf8] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xf9] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xfa] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xfb] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xfc] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xfd] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xfe] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xff] = EVENT_END; // duplicated
	}
	else if (version == AKAOSNES_V3) {
		// V3-V4 common
		pSeqFile->EventMap[0xd2] = EVENT_VOLUME;
		pSeqFile->EventMap[0xd3] = EVENT_VOLUME_FADE;
		pSeqFile->EventMap[0xd4] = EVENT_PAN;
		pSeqFile->EventMap[0xd5] = EVENT_PAN_FADE;
		pSeqFile->EventMap[0xd6] = EVENT_PITCH_SLIDE;
		pSeqFile->EventMap[0xd7] = EVENT_VIBRATO_ON;
		pSeqFile->EventMap[0xd8] = EVENT_VIBRATO_OFF;
		pSeqFile->EventMap[0xd9] = EVENT_TREMOLO_ON;
		pSeqFile->EventMap[0xda] = EVENT_TREMOLO_OFF;
		pSeqFile->EventMap[0xdb] = EVENT_PAN_LFO_ON;
		pSeqFile->EventMap[0xdc] = EVENT_PAN_LFO_OFF;
		pSeqFile->EventMap[0xdd] = EVENT_NOISE_FREQ;
		pSeqFile->EventMap[0xde] = EVENT_NOISE_ON;
		pSeqFile->EventMap[0xdf] = EVENT_NOISE_OFF;
		pSeqFile->EventMap[0xe0] = EVENT_PITCHMOD_ON;
		pSeqFile->EventMap[0xe1] = EVENT_PITCHMOD_OFF;
		pSeqFile->EventMap[0xe2] = EVENT_ECHO_ON;
		pSeqFile->EventMap[0xe3] = EVENT_ECHO_OFF;
		pSeqFile->EventMap[0xe4] = EVENT_OCTAVE;
		pSeqFile->EventMap[0xe5] = EVENT_OCTAVE_UP;
		pSeqFile->EventMap[0xe6] = EVENT_OCTAVE_DOWN;
		pSeqFile->EventMap[0xe7] = EVENT_TRANSPOSE_ABS;
		pSeqFile->EventMap[0xe8] = EVENT_TRANSPOSE_REL;
		pSeqFile->EventMap[0xe9] = EVENT_TUNING;
		pSeqFile->EventMap[0xea] = EVENT_PROGCHANGE;
		pSeqFile->EventMap[0xeb] = EVENT_ADSR_AR;
		pSeqFile->EventMap[0xec] = EVENT_ADSR_DR;
		pSeqFile->EventMap[0xed] = EVENT_ADSR_SL;
		pSeqFile->EventMap[0xee] = EVENT_ADSR_SR;
		pSeqFile->EventMap[0xef] = EVENT_ADSR_DEFAULT;
		pSeqFile->EventMap[0xf0] = EVENT_LOOP_START;
		pSeqFile->EventMap[0xf1] = EVENT_LOOP_END;

		// V3 specific
		pSeqFile->EventMap[0xf2] = EVENT_END;
		pSeqFile->EventMap[0xf3] = EVENT_TEMPO;
		pSeqFile->EventMap[0xf4] = EVENT_TEMPO_FADE;
		pSeqFile->EventMap[0xf5] = EVENT_ECHO_VOLUME; // No Operation in Seiken Densetsu 2
		pSeqFile->EventMap[0xf6] = EVENT_ECHO_VOLUME_FADE; // No Operation in Seiken Densetsu 2
		pSeqFile->EventMap[0xf7] = EVENT_ECHO_FEEDBACK_FIR; // No Operation in Seiken Densetsu 2
		pSeqFile->EventMap[0xf8] = EVENT_MASTER_VOLUME;
		pSeqFile->EventMap[0xf9] = EVENT_CONDITIONAL_JUMP;
		pSeqFile->EventMap[0xfa] = EVENT_GOTO;
		pSeqFile->EventMap[0xfb] = EVENT_CPU_CONTROLED_JUMP;
	}
	else if (version == AKAOSNES_V4) {
		// V3-V4 common
		pSeqFile->EventMap[0xc4] = EVENT_VOLUME;
		pSeqFile->EventMap[0xc5] = EVENT_VOLUME_FADE;
		pSeqFile->EventMap[0xc6] = EVENT_PAN;
		pSeqFile->EventMap[0xc7] = EVENT_PAN_FADE;
		pSeqFile->EventMap[0xc8] = EVENT_PITCH_SLIDE;
		pSeqFile->EventMap[0xc9] = EVENT_VIBRATO_ON;
		pSeqFile->EventMap[0xca] = EVENT_VIBRATO_OFF;
		pSeqFile->EventMap[0xcb] = EVENT_TREMOLO_ON;
		pSeqFile->EventMap[0xcc] = EVENT_TREMOLO_OFF;
		pSeqFile->EventMap[0xcd] = EVENT_PAN_LFO_ON;
		pSeqFile->EventMap[0xce] = EVENT_PAN_LFO_OFF;
		pSeqFile->EventMap[0xcf] = EVENT_NOISE_FREQ;
		pSeqFile->EventMap[0xd0] = EVENT_NOISE_ON;
		pSeqFile->EventMap[0xd1] = EVENT_NOISE_OFF;
		pSeqFile->EventMap[0xd2] = EVENT_PITCHMOD_ON;
		pSeqFile->EventMap[0xd3] = EVENT_PITCHMOD_OFF;
		pSeqFile->EventMap[0xd4] = EVENT_ECHO_ON;
		pSeqFile->EventMap[0xd5] = EVENT_ECHO_OFF;
		pSeqFile->EventMap[0xd6] = EVENT_OCTAVE;
		pSeqFile->EventMap[0xd7] = EVENT_OCTAVE_UP;
		pSeqFile->EventMap[0xd8] = EVENT_OCTAVE_DOWN;
		pSeqFile->EventMap[0xd9] = EVENT_TRANSPOSE_ABS;
		pSeqFile->EventMap[0xda] = EVENT_TRANSPOSE_REL;
		pSeqFile->EventMap[0xdb] = EVENT_TUNING;
		pSeqFile->EventMap[0xdc] = EVENT_PROGCHANGE;
		pSeqFile->EventMap[0xdd] = EVENT_ADSR_AR;
		pSeqFile->EventMap[0xde] = EVENT_ADSR_DR;
		pSeqFile->EventMap[0xdf] = EVENT_ADSR_SL;
		pSeqFile->EventMap[0xe0] = EVENT_ADSR_SR;
		pSeqFile->EventMap[0xe1] = EVENT_ADSR_DEFAULT;
		pSeqFile->EventMap[0xe2] = EVENT_LOOP_START;
		pSeqFile->EventMap[0xe3] = EVENT_LOOP_END;

		// V4 specific
		pSeqFile->EventMap[0xe4] = EVENT_SLUR_ON;
		pSeqFile->EventMap[0xe5] = EVENT_SLUR_OFF;
		pSeqFile->EventMap[0xe6] = EVENT_LEGATO_ON;
		pSeqFile->EventMap[0xe7] = EVENT_LEGATO_OFF;
		pSeqFile->EventMap[0xe8] = EVENT_FORCE_NEXT_NOTE_LENGTH;
		pSeqFile->EventMap[0xe9] = EVENT_JUMP_TO_SFX_LO;
		pSeqFile->EventMap[0xea] = EVENT_JUMP_TO_SFX_HI;
		pSeqFile->EventMap[0xeb] = EVENT_END; // TODO: not always true, see Romancing SaGa 2 and Gun Hazard
		pSeqFile->EventMap[0xec] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xed] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xee] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xef] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xf0] = EVENT_TEMPO;
		pSeqFile->EventMap[0xf1] = EVENT_TEMPO_FADE;
		pSeqFile->EventMap[0xf2] = EVENT_ECHO_VOLUME;
		pSeqFile->EventMap[0xf3] = EVENT_ECHO_VOLUME_FADE;
	}

	// TODO: game-specific mappings
}

uint16_t AkaoSnesSeq::ROMAddressToAPUAddress(uint16_t romAddress)
{
	return (romAddress - addrROMRelocBase) + addrAPURelocBase;
}

uint16_t AkaoSnesSeq::GetShortAddress(uint32_t offset)
{
	return ROMAddressToAPUAddress(GetShort(offset));
}

//  ************
//  AkaoSnesTrack
//  ************

AkaoSnesTrack::AkaoSnesTrack(AkaoSnesSeq* parentFile, long offset, long length)
	: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

void AkaoSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();
}


bool AkaoSnesTrack::ReadEvent(void)
{
	AkaoSnesSeq* parentSeq = (AkaoSnesSeq*)this->parentSeq;

	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000) {
		return false;
	}

	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	std::wstringstream desc;

	AkaoSnesSeqEventType eventType = (AkaoSnesSeqEventType)0;
	std::map<uint8_t, AkaoSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

	case EVENT_NOP1:
	{
		curOffset++;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"NOP", desc.str().c_str(), CLR_MISC, ICON_BINARY);
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
		uint16_t fadeLength;
		if (parentSeq->version == AKAOSNES_V1) {
			fadeLength = GetShort(curOffset); curOffset += 2;
		}
		else {
			fadeLength = GetByte(curOffset++);
		}
		uint8_t vol = GetByte(curOffset++);

		desc << L"Fade Length: " << (int)fadeLength << L"  Volume: " << (int)vol;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Volume Fade", desc.str().c_str(), CLR_VOLUME, ICON_CONTROL);
		break;
	}

	default:
		desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
		pRoot->AddLogItem(new LogItem(std::wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, std::wstring(L"AkaoSnesSeq")));
		bContinue = false;
		break;
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}

uint16_t AkaoSnesTrack::ROMAddressToAPUAddress(uint16_t romAddress)
{
	AkaoSnesSeq* parentSeq = (AkaoSnesSeq*)this->parentSeq;
	return parentSeq->ROMAddressToAPUAddress(romAddress);
}

uint16_t AkaoSnesTrack::GetShortAddress(uint32_t offset)
{
	return ROMAddressToAPUAddress(GetShort(offset));
}
