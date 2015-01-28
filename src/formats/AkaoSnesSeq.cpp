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

	// opcode min/max
	switch (version) {
	case AKAOSNES_V1:
		STATUS_NOTE_MAX = 0xd1;
		STATUS_NOTEINDEX_REST = 12;
		STATUS_NOTEINDEX_TIE = 13;
		break;

	case AKAOSNES_V2:
		STATUS_NOTE_MAX = 0xd1;
		STATUS_NOTEINDEX_TIE = 12;
		STATUS_NOTEINDEX_REST = 13;
		break;

	case AKAOSNES_V3:
		STATUS_NOTE_MAX = 0xd1;
		STATUS_NOTEINDEX_TIE = 12;
		STATUS_NOTEINDEX_REST = 13;
		break;

	case AKAOSNES_V4:
		STATUS_NOTE_MAX = 0xc3;
		STATUS_NOTEINDEX_TIE = 12;
		STATUS_NOTEINDEX_REST = 13;
		break;
	}

	// duration table
	if (version == AKAOSNES_V1) {
		const uint8_t NOTE_DUR_TABLE_FF4[15] = {
			0xc0, 0x90, 0x60, 0x48, 0x40, 0x30, 0x24, 0x20, 0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03
		};
		NOTE_DUR_TABLE.assign(std::begin(NOTE_DUR_TABLE_FF4), std::end(NOTE_DUR_TABLE_FF4));
	}
	else if (version == AKAOSNES_V2 || version == AKAOSNES_V3) {
		const uint8_t NOTE_DUR_TABLE_RS1[15] = {
			0xc0, 0x90, 0x60, 0x40, 0x48, 0x30, 0x20, 0x24, 0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03
		};
		NOTE_DUR_TABLE.assign(std::begin(NOTE_DUR_TABLE_RS1), std::end(NOTE_DUR_TABLE_RS1));
	}
	else { // AKAOSNES_V4
		const uint8_t NOTE_DUR_TABLE_RS2[14] = {
			0xc0, 0x60, 0x40, 0x48, 0x30, 0x20, 0x24, 0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03
		};
		NOTE_DUR_TABLE.assign(std::begin(NOTE_DUR_TABLE_RS2), std::end(NOTE_DUR_TABLE_RS2));
	}

	// timer 0 frequency
	if (version == AKAOSNES_V4) {
		if (minorVersion == AKAOSNES_V4_RS2 || minorVersion == AKAOSNES_V4_LAL) {
			TIMER0_FREQUENCY = 0x24;
		}
		else if (minorVersion == AKAOSNES_V4_FM || minorVersion == AKAOSNES_V4_CT) {
			TIMER0_FREQUENCY = 0x2a;
		}
		else {
			TIMER0_FREQUENCY = 0x27;
		}
	}
	else { // AKAOSNES_V1, AKAOSNES_V2, AKAOSNES_V3
		TIMER0_FREQUENCY = 0x24;
	}

	// pan bitdepth
	if (version == AKAOSNES_V1 || version == AKAOSNES_V3) {
		PAN_8BIT = true;
	}
	else if (version == AKAOSNES_V2) {
		PAN_8BIT = false;
	}
	else { // AKAOSNES_V4
		if (minorVersion == AKAOSNES_V4_RS2 || minorVersion == AKAOSNES_V4_LAL) {
			PAN_8BIT = true;
		}
		else {
			PAN_8BIT = false;
		}
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
		pSeqFile->EventMap[0xde] = EVENT_DURATION_RATE;
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
		pSeqFile->EventMap[0xf5] = EVENT_LOOP_BREAK;
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
		pSeqFile->EventMap[0xf0] = EVENT_LOOP_BREAK;
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

		// V3 common
		pSeqFile->EventMap[0xf2] = EVENT_END;
		pSeqFile->EventMap[0xf3] = EVENT_TEMPO;
		pSeqFile->EventMap[0xf4] = EVENT_TEMPO_FADE;
		pSeqFile->EventMap[0xf5] = EVENT_ECHO_VOLUME; // No Operation in Seiken Densetsu 2
		pSeqFile->EventMap[0xf6] = EVENT_ECHO_VOLUME_FADE; // No Operation in Seiken Densetsu 2
		pSeqFile->EventMap[0xf7] = EVENT_ECHO_FEEDBACK_FIR; // No Operation in Seiken Densetsu 2
		pSeqFile->EventMap[0xf8] = EVENT_MASTER_VOLUME;
		pSeqFile->EventMap[0xf9] = EVENT_LOOP_BREAK;
		pSeqFile->EventMap[0xfa] = EVENT_GOTO;
		pSeqFile->EventMap[0xfb] = EVENT_CPU_CONTROLED_JUMP;

		// game-specific mappings
		if (minorVersion == AKAOSNES_V3_SD2) {
			//pSeqFile->EventMap[0xfc] = (AkaoSnesSeqEventType)0;
			//pSeqFile->EventMap[0xfd] = (AkaoSnesSeqEventType)0;
			pSeqFile->EventMap[0xfe] = EVENT_END;
			pSeqFile->EventMap[0xff] = EVENT_END;
		}
		else {
			pSeqFile->EventMap[0xfc] = EVENT_END;
			pSeqFile->EventMap[0xfd] = EVENT_END;
			pSeqFile->EventMap[0xfe] = EVENT_END;
			pSeqFile->EventMap[0xff] = EVENT_END;
		}
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

		// V4 common
		pSeqFile->EventMap[0xe4] = EVENT_SLUR_ON;
		pSeqFile->EventMap[0xe5] = EVENT_SLUR_OFF;
		pSeqFile->EventMap[0xe6] = EVENT_LEGATO_ON;
		pSeqFile->EventMap[0xe7] = EVENT_LEGATO_OFF;
		pSeqFile->EventMap[0xe8] = EVENT_ONETIME_DURATION;
		pSeqFile->EventMap[0xe9] = EVENT_JUMP_TO_SFX_LO;
		pSeqFile->EventMap[0xea] = EVENT_JUMP_TO_SFX_HI;
		pSeqFile->EventMap[0xeb] = EVENT_END; // exception: Gun Hazard
		pSeqFile->EventMap[0xec] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xed] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xee] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xef] = EVENT_END; // duplicated
		pSeqFile->EventMap[0xf0] = EVENT_TEMPO;
		pSeqFile->EventMap[0xf1] = EVENT_TEMPO_FADE;
		pSeqFile->EventMap[0xf2] = EVENT_ECHO_VOLUME;
		pSeqFile->EventMap[0xf3] = EVENT_ECHO_VOLUME_FADE;

		// game-specific mappings
		if (minorVersion == AKAOSNES_V4_RS2) {
			pSeqFile->EventMap[0xf4] = EVENT_ECHO_FEEDBACK_FIR;
			pSeqFile->EventMap[0xf5] = EVENT_MASTER_VOLUME;
			pSeqFile->EventMap[0xf6] = EVENT_LOOP_BREAK;
			pSeqFile->EventMap[0xf7] = EVENT_GOTO;
			pSeqFile->EventMap[0xf8] = EVENT_INC_CPU_SHARED_COUNTER;
			pSeqFile->EventMap[0xf9] = EVENT_ZERO_CPU_SHARED_COUNTER;
			pSeqFile->EventMap[0xfa] = EVENT_MUTE;
			pSeqFile->EventMap[0xfb] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xfc] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xfd] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xfe] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xff] = EVENT_END; // duplicated
		}
		else if (minorVersion == AKAOSNES_V4_LAL) {
			pSeqFile->EventMap[0xf4] = EVENT_ECHO_FEEDBACK_FIR;
			pSeqFile->EventMap[0xf5] = EVENT_MASTER_VOLUME;
			pSeqFile->EventMap[0xf6] = EVENT_LOOP_BREAK;
			pSeqFile->EventMap[0xf7] = EVENT_GOTO;
			pSeqFile->EventMap[0xf8] = EVENT_INC_CPU_SHARED_COUNTER;
			pSeqFile->EventMap[0xf9] = EVENT_ZERO_CPU_SHARED_COUNTER;
			pSeqFile->EventMap[0xfa] = EVENT_IGNORE_MASTER_VOLUME;
			pSeqFile->EventMap[0xfb] = EVENT_CPU_CONTROLED_JUMP;
			pSeqFile->EventMap[0xfc] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xfd] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xfe] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xff] = EVENT_END; // duplicated
		}
		else if (minorVersion == AKAOSNES_V4_FF6) {
			pSeqFile->EventMap[0xf4] = EVENT_MASTER_VOLUME;
			pSeqFile->EventMap[0xf5] = EVENT_LOOP_BREAK;
			pSeqFile->EventMap[0xf6] = EVENT_GOTO;
			pSeqFile->EventMap[0xf7] = EVENT_ECHO_FEEDBACK_FADE;
			pSeqFile->EventMap[0xf8] = EVENT_ECHO_FIR_FADE;
			pSeqFile->EventMap[0xf9] = EVENT_INC_CPU_SHARED_COUNTER;
			pSeqFile->EventMap[0xfa] = EVENT_ZERO_CPU_SHARED_COUNTER;
			pSeqFile->EventMap[0xfb] = EVENT_IGNORE_MASTER_VOLUME;
			pSeqFile->EventMap[0xfc] = EVENT_CPU_CONTROLED_JUMP;
			pSeqFile->EventMap[0xfd] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xfe] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xff] = EVENT_END; // duplicated
		}
		else if (minorVersion == AKAOSNES_V4_FM) {
			pSeqFile->EventMap[0xf4] = EVENT_MASTER_VOLUME;
			pSeqFile->EventMap[0xf5] = EVENT_LOOP_BREAK;
			pSeqFile->EventMap[0xf6] = EVENT_GOTO;
			pSeqFile->EventMap[0xf7] = EVENT_ECHO_FEEDBACK_FADE;
			pSeqFile->EventMap[0xf8] = EVENT_ECHO_FIR_FADE;
			//pSeqFile->EventMap[0xf9] = (AkaoSnesSeqEventType)0;
			pSeqFile->EventMap[0xfa] = EVENT_CPU_CONTROLED_JUMP;
			pSeqFile->EventMap[0xfb] = EVENT_PERC_ON;
			pSeqFile->EventMap[0xfc] = EVENT_PERC_OFF;
			pSeqFile->EventMap[0xfd] = EVENT_VOLUME_ALT;
			pSeqFile->EventMap[0xfe] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xff] = EVENT_END; // duplicated
		}
		else if (minorVersion == AKAOSNES_V4_CT) {
			pSeqFile->EventMap[0xf4] = EVENT_MASTER_VOLUME;
			pSeqFile->EventMap[0xf5] = EVENT_LOOP_BREAK;
			pSeqFile->EventMap[0xf6] = EVENT_GOTO;
			pSeqFile->EventMap[0xf7] = EVENT_ECHO_FEEDBACK_FADE;
			pSeqFile->EventMap[0xf8] = EVENT_ECHO_FIR_FADE;
			pSeqFile->EventMap[0xf9] = EVENT_CPU_CONTROLED_SET_VALUE;
			pSeqFile->EventMap[0xfa] = EVENT_CPU_CONTROLED_JUMP;
			pSeqFile->EventMap[0xfb] = EVENT_PERC_ON;
			pSeqFile->EventMap[0xfc] = EVENT_PERC_OFF;
			pSeqFile->EventMap[0xfd] = EVENT_VOLUME_ALT;
			pSeqFile->EventMap[0xfe] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xff] = EVENT_END; // duplicated
		}
		else if (minorVersion == AKAOSNES_V4_RS3) {
			pSeqFile->EventMap[0xf4] = EVENT_MASTER_VOLUME;
			pSeqFile->EventMap[0xf5] = EVENT_LOOP_BREAK;
			pSeqFile->EventMap[0xf6] = EVENT_GOTO;
			pSeqFile->EventMap[0xf7] = EVENT_ECHO_FEEDBACK;
			pSeqFile->EventMap[0xf8] = EVENT_ECHO_FIR;
			pSeqFile->EventMap[0xf9] = EVENT_CPU_CONTROLED_SET_VALUE;
			pSeqFile->EventMap[0xfa] = EVENT_CPU_CONTROLED_JUMP;
			pSeqFile->EventMap[0xfb] = EVENT_PERC_ON;
			pSeqFile->EventMap[0xfc] = EVENT_PERC_OFF;
			//pSeqFile->EventMap[0xfd] = (AkaoSnesSeqEventType)0;
			pSeqFile->EventMap[0xfe] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xff] = EVENT_END; // duplicated
		}
		else if (minorVersion == AKAOSNES_V4_GH) {
			pSeqFile->EventMap[0xeb] = EVENT_UNKNOWN1;
			pSeqFile->EventMap[0xec] = EVENT_END;

			pSeqFile->EventMap[0xf4] = EVENT_MASTER_VOLUME;
			pSeqFile->EventMap[0xf5] = EVENT_LOOP_BREAK;
			pSeqFile->EventMap[0xf6] = EVENT_GOTO;
			pSeqFile->EventMap[0xf7] = EVENT_ECHO_FEEDBACK;
			pSeqFile->EventMap[0xf8] = EVENT_ECHO_FIR;
			pSeqFile->EventMap[0xf9] = EVENT_CPU_CONTROLED_SET_VALUE;
			pSeqFile->EventMap[0xfa] = EVENT_CPU_CONTROLED_JUMP;
			pSeqFile->EventMap[0xfb] = EVENT_PERC_ON;
			pSeqFile->EventMap[0xfc] = EVENT_PERC_OFF;
			pSeqFile->EventMap[0xfd] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xfe] = EVENT_END; // duplicated
			pSeqFile->EventMap[0xff] = EVENT_END; // duplicated
		}
		else if (minorVersion == AKAOSNES_V4_BSGAME) {
			pSeqFile->EventMap[0xf4] = EVENT_MASTER_VOLUME;
			pSeqFile->EventMap[0xf5] = EVENT_LOOP_BREAK;
			pSeqFile->EventMap[0xf6] = EVENT_GOTO;
			pSeqFile->EventMap[0xf7] = EVENT_ECHO_FEEDBACK;
			pSeqFile->EventMap[0xf8] = EVENT_ECHO_FIR;
			pSeqFile->EventMap[0xf9] = EVENT_CPU_CONTROLED_SET_VALUE;
			pSeqFile->EventMap[0xfa] = EVENT_CPU_CONTROLED_JUMP;
			pSeqFile->EventMap[0xfb] = EVENT_PERC_ON;
			pSeqFile->EventMap[0xfc] = EVENT_PERC_OFF;
			//pSeqFile->EventMap[0xfd] = (AkaoSnesSeqEventType)0;
			//pSeqFile->EventMap[0xfe] = (AkaoSnesSeqEventType)0;
			pSeqFile->EventMap[0xff] = EVENT_END; // duplicated
		}
	}
}

double AkaoSnesSeq::GetTempoInBPM(uint8_t tempo)
{
	if (tempo != 0 && TIMER0_FREQUENCY != 0)
	{
		return 60000000.0 / (SEQ_PPQN * (125 * TIMER0_FREQUENCY)) * (tempo / 256.0);
	}
	else
	{
		return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
	}
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

	vel = 100;
	octave = 6;
	onetimeDuration = 0;
	loopLevel = 0;
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

	case EVENT_NOTE: // 0x00..0xc3
	{
		uint8_t durIndex = statusByte % parentSeq->NOTE_DUR_TABLE.size();
		uint8_t noteIndex = statusByte / parentSeq->NOTE_DUR_TABLE.size();
		uint8_t dur = parentSeq->NOTE_DUR_TABLE[durIndex];

		if (onetimeDuration != 0) {
			dur = onetimeDuration;
			onetimeDuration = 0;
		}

		if (noteIndex < 12) {
			uint8_t note = octave * 12 + noteIndex;

			// TODO: percussion note

			AddNoteByDur(beginOffset, curOffset - beginOffset, note, vel, dur);
			AddTime(dur);
		}
		else if (noteIndex == parentSeq->STATUS_NOTEINDEX_TIE) {
			MakePrevDurNoteEnd();
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str().c_str(), CLR_TIE, ICON_NOTE);
			AddTime(dur);
		}
		else {
			AddRest(beginOffset, curOffset - beginOffset, dur);
		}

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
		// TODO: correct volume bitdepth?
		uint8_t vol = GetByte(curOffset++);
		AddVol(beginOffset, curOffset - beginOffset, vol >> 1);
		break;
	}

	case EVENT_VOLUME_FADE:
	{
		// TODO: correct volume bitdepth?
		uint16_t fadeLength;
		if (parentSeq->version == AKAOSNES_V1) {
			fadeLength = GetShort(curOffset); curOffset += 2;
		}
		else {
			fadeLength = GetByte(curOffset++);
		}
		uint8_t vol = GetByte(curOffset++);

		if (fadeLength != 0) {
			desc << L"Fade Length: " << (int)fadeLength << L"  Volume: " << (int)vol;
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Volume Fade", desc.str().c_str(), CLR_VOLUME, ICON_CONTROL);
		}
		else {
			AddVol(beginOffset, curOffset - beginOffset, vol >> 1);
		}
		break;
	}

	case EVENT_PAN:
	{
		uint8_t pan = GetByte(curOffset++) << (parentSeq->PAN_8BIT ? 0 : 1);
		AddPan(beginOffset, curOffset - beginOffset, pan >> 1);
		break;
	}

	case EVENT_PAN_FADE:
	{
		uint16_t fadeLength;
		if (parentSeq->version == AKAOSNES_V1) {
			fadeLength = GetShort(curOffset); curOffset += 2;
		}
		else {
			fadeLength = GetByte(curOffset++);
		}
		uint8_t pan = GetByte(curOffset++) << (parentSeq->PAN_8BIT ? 0 : 1);

		if (fadeLength != 0) {
			desc << L"Fade Length: " << (int)fadeLength << L"  Pan: " << (int)pan;
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan Fade", desc.str().c_str(), CLR_PAN, ICON_CONTROL);
		}
		else {
			AddPan(beginOffset, curOffset - beginOffset, pan >> 1);
		}
		break;
	}

	case EVENT_PITCH_SLIDE_ON:
	{
		int8_t pitchSlideSemitones;
		uint8_t pitchSlideDelay;
		uint8_t pitchSlideLength;

		if (parentSeq->version == AKAOSNES_V1) {
			pitchSlideDelay = GetByte(curOffset++);
			pitchSlideLength = GetByte(curOffset++);
			pitchSlideSemitones = GetByte(curOffset++);
			desc << L"Delay: " << (int)pitchSlideDelay << L"  Length: " << (int)pitchSlideLength << L"  Key: " << (int)pitchSlideSemitones << L" semitones";
		}
		else { // AKAOSNES_V2
			pitchSlideSemitones = GetByte(curOffset++);
			pitchSlideDelay = GetByte(curOffset++);
			pitchSlideLength = GetByte(curOffset++);
			desc << L"Key: " << (int)pitchSlideSemitones << L" semitones  Delay: " << (int)pitchSlideDelay << L"  Length: " << (int)pitchSlideLength;
		}

		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Slide On", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_SLIDE_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Slide Off", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_PITCH_SLIDE:
	{
		uint8_t pitchSlideLength = GetByte(curOffset++);
		int8_t pitchSlideSemitones = GetByte(curOffset++);
		desc << L"Length: " << (int)pitchSlideLength << L"  Key: " << (int)pitchSlideSemitones << L" semitones";
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Slide", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
		break;
	}

	case EVENT_VIBRATO_ON:
	{
		uint8_t lfoDelay = GetByte(curOffset++);
		uint8_t lfoRate = GetByte(curOffset++);
		uint8_t lfoDepth = GetByte(curOffset++);
		desc << L"Delay: " << (int)lfoDelay << L"  Rate: " << (int)lfoRate << L"  Depth: " << (int)lfoDepth;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_VIBRATO_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Off", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_TREMOLO_ON:
	{
		uint8_t lfoDelay = GetByte(curOffset++);
		uint8_t lfoRate = GetByte(curOffset++);
		uint8_t lfoDepth = GetByte(curOffset++);
		desc << L"Delay: " << (int)lfoDelay << L"  Rate: " << (int)lfoRate << L"  Depth: " << (int)lfoDepth;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_TREMOLO_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo Off", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_PAN_LFO_ON:
	{
		uint8_t lfoDepth = GetByte(curOffset++);
		uint8_t lfoRate = GetByte(curOffset++);
		desc << L"Depth: " << (int)lfoDepth << L"  Rate: " << (int)lfoRate;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan LFO", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_PAN_LFO_ON_WITH_DELAY:
	{
		uint8_t lfoDelay = GetByte(curOffset++);
		uint8_t lfoRate = GetByte(curOffset++);
		uint8_t lfoDepth = GetByte(curOffset++);
		desc << L"Delay: " << (int)lfoDelay << L"  Rate: " << (int)lfoRate << L"  Depth: " << (int)lfoDepth;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan LFO", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_PAN_LFO_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan LFO Off", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
		break;
	}

	case EVENT_NOISE_FREQ:
	{
		uint8_t newNCK = GetByte(curOffset++) & 0x1f;
		desc << L"Noise Frequency (NCK): " << (int)newNCK;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise Frequency", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_NOISE_ON:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise On", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_NOISE_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise Off", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_PITCHMOD_ON:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Modulation On", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_PITCHMOD_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Modulation Off", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
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

	case EVENT_TRANSPOSE_ABS:
	{
		int8_t newTranspose = GetByte(curOffset++);
		AddTranspose(beginOffset, curOffset - beginOffset, newTranspose);
		break;
	}

	case EVENT_TRANSPOSE_REL:
	{
		int8_t newTranspose = GetByte(curOffset++);
		AddTranspose(beginOffset, curOffset - beginOffset, transpose + newTranspose, L"Transpose (Relative)");
		break;
	}

	case EVENT_TUNING:
	{
		int8_t newTuning = GetByte(curOffset++);
		AddFineTuning(beginOffset, curOffset - beginOffset, newTuning / 16.0);
		break;
	}

	case EVENT_PROGCHANGE:
	{
		uint8_t newProg = GetByte(curOffset++);
		AddProgramChange(beginOffset, curOffset - beginOffset, newProg);
		break;
	}

	case EVENT_VOLUME_ENVELOPE:
	{
		uint8_t envelopeIndex = GetByte(curOffset++);
		desc << L"Envelope: " << (int)envelopeIndex;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Volume Envelope", desc.str().c_str(), CLR_VOLUME, ICON_CONTROL);
		break;
	}

	case EVENT_GAIN_RELEASE:
	{
		uint8_t gain = GetByte(curOffset++) & 0x1f;
		desc << L"GAIN: " << (int)gain;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Release Rate (GAIN)", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_DURATION_RATE:
	{
		uint8_t rate = GetByte(curOffset++);
		if (rate == 0 || rate == 100) {
			rate = 100;
		}

		desc << L"Note Length: " << (int)rate << " %";
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration Rate", desc.str().c_str(), CLR_DURNOTE, ICON_CONTROL);
		break;
	}

	case EVENT_ADSR_AR:
	{
		uint8_t newAR = GetByte(curOffset++) & 15;
		desc << L"AR: " << (int)newAR;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Attack Rate", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_ADSR_DR:
	{
		uint8_t newDR = GetByte(curOffset++) & 7;
		desc << L"DR: " << (int)newDR;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Decay Rate", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_ADSR_SL:
	{
		uint8_t newSL = GetByte(curOffset++) & 7;
		desc << L"SL: " << (int)newSL;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Sustain Level", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_ADSR_SR:
	{
		uint8_t newSR = GetByte(curOffset++) & 15;
		desc << L"SR: " << (int)newSR;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Sustain Rate", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_ADSR_DEFAULT:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Default ADSR", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
		break;
	}

	case EVENT_LOOP_START:
	{
		uint8_t count = GetByte(curOffset++);
		if (count != 0) {
			count++;
		}

		desc << L"Loop Count: " << (int)count;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Start", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

		loopStart[loopLevel] = curOffset;
		if (parentSeq->version == AKAOSNES_V4) {
			loopIncCount[loopLevel] = 1;
		}
		else { // AKAOSNES_V1, AKAOSNES_V2, AKAOSNES_V3
			loopIncCount[loopLevel] = 0;
		}
		loopDecCount[loopLevel] = count;
		loopLevel = (loopLevel + 1) % AKAOSNES_LOOP_LEVEL_MAX;
		break;
	}

	case EVENT_LOOP_END:
	{
		uint8_t prevLoopLevel = (loopLevel != 0 ? loopLevel : AKAOSNES_LOOP_LEVEL_MAX) - 1;
		if (parentSeq->version == AKAOSNES_V4) {
			loopIncCount[prevLoopLevel]++;
		}

		if (loopDecCount[prevLoopLevel] == 0) {
			// infinite loop
			bContinue = AddLoopForever(beginOffset, curOffset - beginOffset);
			curOffset = loopStart[prevLoopLevel];
		}
		else {
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop End", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

			if (loopDecCount[prevLoopLevel] - 1 == 0) {
				// repeat end
				loopLevel = prevLoopLevel;
			}
			else {
				// repeat again
				loopDecCount[prevLoopLevel]--;
				curOffset = loopStart[prevLoopLevel];
			}
		}

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

	case EVENT_LEGATO_ON:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Legato On", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_LEGATO_OFF:
	{
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Legato Off", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
		break;
	}

	case EVENT_ONETIME_DURATION:
	{
		uint8_t dur = GetByte(curOffset++);
		onetimeDuration = dur;
		desc << L"Duration: " << (int)dur;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration (One-Time)", desc.str().c_str(), CLR_DURNOTE);
		break;
	}

	case EVENT_JUMP_TO_SFX_LO:
	{
		// TODO: EVENT_JUMP_TO_SFX_LO
		uint8_t sfxIndex = GetByte(curOffset++);
		desc << L"SFX: " << (int)sfxIndex;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Jump to SFX (LOWORD)", desc.str().c_str());
		bContinue = false;
		break;
	}

	case EVENT_JUMP_TO_SFX_HI:
	{
		// TODO: EVENT_JUMP_TO_SFX_HI
		uint8_t sfxIndex = GetByte(curOffset++);
		desc << L"SFX: " << (int)sfxIndex;
		AddUnknown(beginOffset, curOffset - beginOffset, L"Jump to SFX (HIWORD)", desc.str().c_str());
		bContinue = false;
		break;
	}

	case EVENT_END:
	{
		AddEndOfTrack(beginOffset, curOffset - beginOffset);
		bContinue = false;
		break;
	}

	case EVENT_TEMPO:
	{
		uint8_t newTempo = GetByte(curOffset++);
		if (parentSeq->minorVersion == AKAOSNES_V4_FM || parentSeq->minorVersion == AKAOSNES_V4_CT) {
			newTempo += (newTempo * 0x14) >> 8;
		}
		AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
		break;
	}

	case EVENT_TEMPO_FADE:
	{
		uint16_t fadeLength;
		if (parentSeq->version == AKAOSNES_V1) {
			fadeLength = GetShort(curOffset); curOffset += 2;
		}
		else {
			fadeLength = GetByte(curOffset++);
		}

		uint8_t newTempo = GetByte(curOffset++);
		if (parentSeq->minorVersion == AKAOSNES_V4_FM || parentSeq->minorVersion == AKAOSNES_V4_CT) {
			newTempo += (newTempo * 0x14) >> 8;
		}

		if (fadeLength != 0) {
			desc << L"Fade Length: " << (int)fadeLength << L"  BPM: " << parentSeq->GetTempoInBPM(newTempo);
			AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tempo Fade", desc.str().c_str(), CLR_TEMPO, ICON_TEMPO);
		}
		else {
			AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
		}
		break;
	}

	case EVENT_ECHO_VOLUME:
	{
		uint8_t vol = GetByte(curOffset++);
		desc << L"Volume: " << (int)vol;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Volume", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_VOLUME_FADE:
	{
		uint16_t fadeLength = GetByte(curOffset++);
		int8_t vol = GetByte(curOffset++);

		desc << L"Fade Length: " << (int)fadeLength << L"  Volume: " << (int)vol;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Volume Fade", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_FEEDBACK_FIR:
	{
		int8_t feedback = GetByte(curOffset++);
		uint8_t filterIndex = GetByte(curOffset++);
		desc << L"Feedback: " << (int)feedback << L"  FIR: " << (int)filterIndex;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Feedback & FIR", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_MASTER_VOLUME:
	{
		uint8_t newVol = GetByte(curOffset++);
		AddMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
		break;
	}

	case EVENT_LOOP_BREAK:
	{
		uint8_t count = GetByte(curOffset++);
		uint16_t dest = GetShortAddress(curOffset); curOffset += 2;

		desc << L"Count: " << (int)count << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Break / Jump to Volta", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

		uint8_t prevLoopLevel = (loopLevel != 0 ? loopLevel : AKAOSNES_LOOP_LEVEL_MAX) - 1;
		if (parentSeq->version != AKAOSNES_V4) { // AKAOSNES_V1, AKAOSNES_V2, AKAOSNES_V3
			loopIncCount[prevLoopLevel]++;
		}

		if (count == loopIncCount[prevLoopLevel]) {
			if (loopDecCount[prevLoopLevel] - 1 == 0) {
				loopLevel = prevLoopLevel;
			}
			curOffset = dest;
		}

		break;
	}

	case EVENT_GOTO:
	{
		uint16_t dest = GetShortAddress(curOffset); curOffset += 2;
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

	case EVENT_INC_CPU_SHARED_COUNTER:
	{
		AddUnknown(beginOffset, curOffset - beginOffset, L"Increment CPU-Shared Counter", desc.str().c_str());
		break;
	}

	case EVENT_ZERO_CPU_SHARED_COUNTER:
	{
		AddUnknown(beginOffset, curOffset - beginOffset, L"Zero CPU-Shared Counter", desc.str().c_str());
		break;
	}

	case EVENT_ECHO_FEEDBACK_FADE:
	{
		uint16_t fadeLength = GetByte(curOffset++);
		uint8_t feedback = GetByte(curOffset++);

		desc << L"Fade Length: " << (int)fadeLength << L"  Feedback: " << (int)feedback;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Feedback Fade", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_FIR_FADE:
	{
		uint16_t fadeLength = GetByte(curOffset++);
		uint8_t filterIndex = GetByte(curOffset++);

		desc << L"Fade Length: " << (int)fadeLength << L"  FIR: " << (int)vol;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo FIR Fade", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_FEEDBACK:
	{
		uint8_t feedback = GetByte(curOffset++);
		desc << L"Feedback: " << (int)feedback;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Feedback", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_ECHO_FIR:
	{
		uint8_t filterIndex = GetByte(curOffset++);
		desc << L"FIR: " << (int)vol;
		AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo FIR", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
		break;
	}

	case EVENT_CPU_CONTROLED_SET_VALUE:
	{
		uint8_t value = GetByte(curOffset++);
		AddUnknown(beginOffset, curOffset - beginOffset, L"Set Value for CPU-Controled Jump", desc.str().c_str());
		break;
	}

	case EVENT_CPU_CONTROLED_JUMP:
	{
		uint8_t arg1 = GetByte(curOffset++) & 15;
		uint16_t dest = GetShortAddress(curOffset); curOffset += 2;
		desc << L"Arg1: " << (int)arg1 << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
		AddUnknown(beginOffset, curOffset - beginOffset, L"CPU-Controled Jump", desc.str().c_str());
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

	case EVENT_MUTE:
	{
		AddUnknown(beginOffset, curOffset - beginOffset, L"Mute Channel", desc.str().c_str());
		break;
	}

	case EVENT_IGNORE_MASTER_VOLUME:
	{
		AddUnknown(beginOffset, curOffset - beginOffset, L"Ignore Master Volume", desc.str().c_str());
		break;
	}

	case EVENT_VOLUME_ALT:
	{
		uint8_t vol = GetByte(curOffset++) & 0x7f;
		AddExpression(beginOffset, curOffset - beginOffset, vol);
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
