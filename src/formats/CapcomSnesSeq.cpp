#include "stdafx.h"
#include "CapcomSnesSeq.h"
#include "CapcomSnesFormat.h"
#include "ScaleConversion.h"

using namespace std;

DECLARE_FORMAT(CapcomSnes);

//  **********
//  CapcomSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    48
#define SEQ_KEYOFS  0

// volume table
const uint8_t CapcomSnesSeq::volTable[] = {
	0x00, 0x0c, 0x19, 0x26, 0x33, 0x40, 0x4c, 0x59,
	0x66, 0x73, 0x80, 0x8c, 0x99, 0xb3, 0xcc, 0xe6,
	0xff,
};

// pan table (compatible with Nintendo engine)
const uint8_t CapcomSnesSeq::panTable[] = {
	0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
	0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
	0x7a, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f,
};

CapcomSnesSeq::CapcomSnesSeq(RawFile* file, CapcomSnesVersion ver, uint32_t seqdataOffset, bool priorityInHeader, wstring newName)
: VGMSeq(CapcomSnesFormat::name, file, seqdataOffset), version(ver), priorityInHeader(priorityInHeader)
{
	name = newName;

	bAllowDiscontinuousTrackData = true;

	LoadEventMap(this);
}

CapcomSnesSeq::~CapcomSnesSeq(void)
{
}

void CapcomSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();

	midiReverb = 40;
	transpose = 0;
}

bool CapcomSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* seqHeader = AddHeader(dwOffset, (priorityInHeader ? 1 : 0) + MAX_TRACKS * 2, L"Sequence Header");
	uint32_t curHeaderOffset = dwOffset;

	if (priorityInHeader) {
		seqHeader->AddSimpleItem(curHeaderOffset, 1, L"Priority");
		curHeaderOffset++;
	}

	for (int i = 0; i < MAX_TRACKS; i++)
	{
		uint16_t trkOff = GetShortBE(curHeaderOffset);
		seqHeader->AddPointer(curHeaderOffset, 2, trkOff, true, L"Track Pointer");
		curHeaderOffset += 2;
	}

	return true;		//successful
}


bool CapcomSnesSeq::GetTrackPointers(void)
{
	for (int i = MAX_TRACKS - 1; i >= 0; i--)
	{
		uint16_t trkOff = GetShortBE(dwOffset + (priorityInHeader ? 1 : 0) + i * 2);
		if (trkOff != 0)
			aTracks.push_back(new CapcomSnesTrack(this, trkOff));
	}
	return true;
}

void CapcomSnesSeq::LoadEventMap(CapcomSnesSeq *pSeqFile)
{
	pSeqFile->EventMap[0x00] = EVENT_TOGGLE_TRIPLET;
	pSeqFile->EventMap[0x01] = EVENT_TOGGLE_SLUR;
	pSeqFile->EventMap[0x02] = EVENT_DOTTED_NOTE_ON;
	pSeqFile->EventMap[0x03] = EVENT_TOGGLE_OCTAVE_UP;
	pSeqFile->EventMap[0x04] = EVENT_NOTE_ATTRIBUTES;
	pSeqFile->EventMap[0x05] = EVENT_TEMPO;
	pSeqFile->EventMap[0x06] = EVENT_DURATION;
	pSeqFile->EventMap[0x07] = EVENT_VOLUME;
	pSeqFile->EventMap[0x08] = EVENT_PROGRAM_CHANGE;
	pSeqFile->EventMap[0x09] = EVENT_OCTAVE;
	pSeqFile->EventMap[0x0a] = EVENT_GLOBAL_TRANSPOSE;
	pSeqFile->EventMap[0x0b] = EVENT_TRANSPOSE;
	pSeqFile->EventMap[0x0c] = EVENT_TUNING;
	pSeqFile->EventMap[0x0d] = EVENT_PORTAMENTO_TIME;
	pSeqFile->EventMap[0x0e] = EVENT_REPEAT_UNTIL_1;
	pSeqFile->EventMap[0x0f] = EVENT_REPEAT_UNTIL_2;
	pSeqFile->EventMap[0x10] = EVENT_REPEAT_UNTIL_3;
	pSeqFile->EventMap[0x11] = EVENT_REPEAT_UNTIL_4;
	pSeqFile->EventMap[0x12] = EVENT_REPEAT_BREAK_1;
	pSeqFile->EventMap[0x13] = EVENT_REPEAT_BREAK_2;
	pSeqFile->EventMap[0x14] = EVENT_REPEAT_BREAK_3;
	pSeqFile->EventMap[0x15] = EVENT_REPEAT_BREAK_4;
	pSeqFile->EventMap[0x16] = EVENT_GOTO;
	pSeqFile->EventMap[0x17] = EVENT_END;
	pSeqFile->EventMap[0x18] = EVENT_PAN;
	pSeqFile->EventMap[0x19] = EVENT_MASTER_VOLUME;
	pSeqFile->EventMap[0x1a] = EVENT_LFO;
	pSeqFile->EventMap[0x1b] = EVENT_ECHO_PARAM;
	pSeqFile->EventMap[0x1c] = EVENT_ECHO_ONOFF;
	pSeqFile->EventMap[0x1d] = EVENT_RELEASE_RATE;
	pSeqFile->EventMap[0x1e] = EVENT_NOP;
	pSeqFile->EventMap[0x1f] = EVENT_NOP;

	switch(pSeqFile->version)
	{
	case V1_BGM_IN_LIST:
		pSeqFile->EventMap[0x1e] = EVENT_UNKNOWN1;
		pSeqFile->EventMap[0x1f] = EVENT_UNKNOWN1;
		break;
	}
}

double CapcomSnesSeq::GetTempoInBPM ()
{
	return GetTempoInBPM(tempo);
}

double CapcomSnesSeq::GetTempoInBPM (uint16_t tempo)
{
	if (tempo != 0)
	{
		return 60000000.0 / (SEQ_PPQN * (125 * 0x40) * 2) * (tempo / 256.0);
	}
	else
	{
		return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
	}
}


//  ************
//  CapcomSnesTrack
//  ************

CapcomSnesTrack::CapcomSnesTrack(CapcomSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

bool CapcomSnesTrack::LoadTrackInit(uint32_t trackNum)
{
	if (!SeqTrack::LoadTrackInit(trackNum))
		return false;

	AddReverbNoItem(0);
	return true;
}

void CapcomSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	cKeyCorrection = SEQ_KEYOFS;

	noteAttributes = 0;
	durationRate = 0;
	transpose = 0;
    for (int i = 0; i < CAPCOM_SNES_REPEAT_SLOT_MAX; i++) {
        repeatCount[i] = 0;
	}
}


uint8_t CapcomSnesTrack::getNoteOctave(void)
{
	return noteAttributes & CAPCOM_SNES_MASK_NOTE_OCTAVE;
}

void CapcomSnesTrack::setNoteOctave(uint8_t octave)
{
	noteAttributes = (noteAttributes & ~CAPCOM_SNES_MASK_NOTE_OCTAVE) | (octave & CAPCOM_SNES_MASK_NOTE_OCTAVE);
}

bool CapcomSnesTrack::isNoteOctaveUp(void)
{
	return (noteAttributes & CAPCOM_SNES_MASK_NOTE_OCTAVE_UP) != 0;
}

void CapcomSnesTrack::setNoteOctaveUp(bool octave_up)
{
	if (octave_up) {
		noteAttributes |= CAPCOM_SNES_MASK_NOTE_OCTAVE_UP;
	}
	else {
		noteAttributes &= ~CAPCOM_SNES_MASK_NOTE_OCTAVE_UP;
	}
}

bool CapcomSnesTrack::isNoteDotted(void)
{
	return (noteAttributes & CAPCOM_SNES_MASK_NOTE_DOTTED) != 0;
}

void CapcomSnesTrack::setNoteDotted(bool dotted)
{
	if (dotted) {
		noteAttributes |= CAPCOM_SNES_MASK_NOTE_DOTTED;
	}
	else {
		noteAttributes &= ~CAPCOM_SNES_MASK_NOTE_DOTTED;
	}
}

bool CapcomSnesTrack::isNoteTriplet(void)
{
	return (noteAttributes & CAPCOM_SNES_MASK_NOTE_TRIPLET) != 0;
}

void CapcomSnesTrack::setNoteTriplet(bool triplet)
{
	if (triplet) {
		noteAttributes |= CAPCOM_SNES_MASK_NOTE_TRIPLET;
	}
	else {
		noteAttributes &= ~CAPCOM_SNES_MASK_NOTE_TRIPLET;
	}
}

bool CapcomSnesTrack::isNoteSlurred(void)
{
	return (noteAttributes & CAPCOM_SNES_MASK_NOTE_SLURRED) != 0;
}

void CapcomSnesTrack::setNoteSlurred(bool slurred)
{
	if (slurred) {
		noteAttributes |= CAPCOM_SNES_MASK_NOTE_SLURRED;
	}
	else {
		noteAttributes &= ~CAPCOM_SNES_MASK_NOTE_SLURRED;
	}
}

double CapcomSnesTrack::GetTuningInSemitones(int8_t tuning)
{
	return tuning / 256.0;
}

#define EVENT_WITH_MIDITEXT_START	bWriteGenericEventAsTextEventTmp = bWriteGenericEventAsTextEvent; bWriteGenericEventAsTextEvent = true;
#define EVENT_WITH_MIDITEXT_END	bWriteGenericEventAsTextEvent = bWriteGenericEventAsTextEventTmp;

bool CapcomSnesTrack::ReadEvent(void)
{
	CapcomSnesSeq* parentSeq = (CapcomSnesSeq*)this->parentSeq;
	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000)
	{
		return false;
	}

	bool bWriteGenericEventAsTextEventTmp;
	uint8_t statusByte = GetByte(curOffset++);
	bool bContinue = true;

	wstringstream desc;

	if (statusByte >= 0x20)
	{
		uint8_t keyIndex = statusByte & 0x1f;
		uint8_t lenIndex = statusByte >> 5;
		bool rest = (keyIndex == 0);

		// calcurate actual note length:
		// actual music engine acquires the length from a table,
		// but it can be calculated quite easily. Here it is.
		uint8_t len = 192 >> (7 - lenIndex);
		if (isNoteDotted())
		{
			if (len % 2 == 0 && len < 0x80)
			{
				len = len + (len / 2);
			}
			else
			{
				// error: note length is not a byte value.
				len = 0;
				pRoot->AddLogItem(new LogItem(L"Note length overflow\n", LOG_LEVEL_WARN, L"CapcomSnesSeq"));
			}
			setNoteDotted(false);
		}
		else if (isNoteTriplet())
		{
			len = len * 2 / 3;
		}

		if (rest)
		{
			AddRest(beginOffset, curOffset-beginOffset, len);
		}
		else
		{
			// calculate duration of note:
			// actual music engine does it in 16 bit precision, because of tempo handling.
			uint16_t dur = len * durationRate;

			if (isNoteSlurred())
			{
				// slurred/tied note must be full-length.
				// TODO: handle tied note!
				dur = len << 8;
			}
			else
			{
				// if 0, it will probably become full-length,
				// because the music engine thinks the note is already off (release time).
				if (dur == 0)
				{
					dur = len << 8;
				}
			}

			// duration - bit reduction for MIDI
			dur = (dur + 0x80) >> 8;
			if (dur == 0)
			{
				dur = 1;
			}

			uint8_t key = (keyIndex - 1) + (getNoteOctave() * 12) + (isNoteOctaveUp() ? 24 : 0);
			uint8_t vel = 127;
			AddNoteByDur(beginOffset, curOffset-beginOffset, key, vel, dur);
			AddTime(len);
		}
	}
	else
	{
		CapcomSnesSeqEventType eventType = (CapcomSnesSeqEventType)0;
		map<uint8_t, CapcomSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

		case EVENT_TOGGLE_TRIPLET:
			setNoteTriplet(!isNoteTriplet());
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Toggle Triplet", NULL, CLR_DURNOTE, ICON_CONTROL);
			break;

		case EVENT_TOGGLE_SLUR:
			setNoteSlurred(!isNoteSlurred());
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Toggle Slur/Tie", NULL, CLR_DURNOTE, ICON_CONTROL);
			break;

		case EVENT_DOTTED_NOTE_ON:
			setNoteDotted(true);
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Dotted Note On", NULL, CLR_DURNOTE, ICON_CONTROL);
			break;

		case EVENT_TOGGLE_OCTAVE_UP:
			setNoteOctaveUp(!isNoteOctaveUp());
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Toggle 2-Octave Up", NULL, CLR_DURNOTE, ICON_CONTROL);
			break;

		case EVENT_NOTE_ATTRIBUTES:
		{
			uint8_t attributes = GetByte(curOffset++);
			noteAttributes &= ~(CAPCOM_SNES_MASK_NOTE_OCTAVE_UP | CAPCOM_SNES_MASK_NOTE_TRIPLET | CAPCOM_SNES_MASK_NOTE_SLURRED);
			noteAttributes |= attributes;
			desc << L"Triplet: " << (isNoteTriplet() ? L"On" : L"Off") << L"  " << L"Slur: " << (isNoteSlurred() ? L"On" : L"Off") << L"  " << L"2-Octave Up: " << (isNoteOctaveUp() ? L"On" : L"Off");
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Note Attributes", desc.str().c_str(), CLR_DURNOTE, ICON_CONTROL);
			break;
		}

		case EVENT_TEMPO:
		{
			uint16_t newTempo = GetShortBE(curOffset); curOffset += 2;
			parentSeq->tempo = newTempo;
			AddTempoBPM(beginOffset, curOffset-beginOffset, parentSeq->GetTempoInBPM());
			break;
		}

		case EVENT_DURATION:
		{
			uint8_t newDurationRate = GetByte(curOffset++);
			durationRate = newDurationRate;
			desc << L"Duration: " << (int)newDurationRate << L"/256";
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Duration", desc.str().c_str(), CLR_DURNOTE, ICON_CONTROL);
			break;
		}

		case EVENT_VOLUME:
		{
			uint8_t newVolume = GetByte(curOffset++);
			uint8_t midiVolume;

			if (parentSeq->version == V1_BGM_IN_LIST)
			{
				// linear volume
				midiVolume = newVolume >> 1;
			}
			else
			{
				// use volume table (with linear interpolation)
				uint8_t volIndex = (newVolume * 16) >> 8;
				uint8_t volRate = (newVolume * 16) & 0xff;
				midiVolume = CapcomSnesSeq::volTable[volIndex] + ((CapcomSnesSeq::volTable[volIndex + 1] - CapcomSnesSeq::volTable[volIndex]) * volRate / 256);
			}
			midiVolume = Convert7bitPercentVolValToStdMidiVal(midiVolume >> 1);

			AddVol(beginOffset, curOffset-beginOffset, newVolume);
			break;
		}

		case EVENT_PROGRAM_CHANGE:
		{
			uint8_t newProg = GetByte(curOffset++);
			AddProgramChange(beginOffset, curOffset-beginOffset, newProg, true);
			break;
		}

		case EVENT_OCTAVE:
		{
			uint8_t newOctave = GetByte(curOffset++);
			desc << L"Octave: " << (int)newOctave;
			setNoteOctave(newOctave);
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Octave", desc.str().c_str(), CLR_DURNOTE, ICON_CONTROL);
			break;
		}

		case EVENT_GLOBAL_TRANSPOSE:
		{
			int8_t newTranspose = GetByte(curOffset++);
			AddGlobalTranspose(beginOffset, curOffset-beginOffset, newTranspose);
			break;
		}

		case EVENT_TRANSPOSE:
		{
			int8_t newTranspose = GetByte(curOffset++);
			AddTranspose(beginOffset, curOffset-beginOffset, newTranspose);
			break;
		}

		case EVENT_TUNING:
		{
			int8_t newTuning = (int8_t) GetByte(curOffset++);
			desc << L"Tuning: " << (int)newTuning << L" (" << (int)(GetTuningInSemitones(newTuning) * 100 + 0.5) << L" cents)";
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tuning", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_PORTAMENTO_TIME:
		{
			// TODO: calculate portamento time in milliseconds
			uint8_t newPortamentoTime = GetByte(curOffset++);
			desc << L"Time: " << (int)newPortamentoTime;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Portamento Time", desc.str().c_str(), CLR_PORTAMENTOTIME, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_REPEAT_UNTIL_1:
		case EVENT_REPEAT_UNTIL_2:
		case EVENT_REPEAT_UNTIL_3:
		case EVENT_REPEAT_UNTIL_4:
		{
			uint8_t times = GetByte(curOffset++);
			uint16_t dest = GetShortBE(curOffset); curOffset += 2;

			uint8_t repeatSlot;
			wchar_t * repeatEventName;
			switch (eventType)
			{
			case EVENT_REPEAT_UNTIL_1: repeatSlot = 0; repeatEventName = L"Repeat Until #1"; break;
			case EVENT_REPEAT_UNTIL_2: repeatSlot = 1; repeatEventName = L"Repeat Until #2"; break;
			case EVENT_REPEAT_UNTIL_3: repeatSlot = 2; repeatEventName = L"Repeat Until #3"; break;
			case EVENT_REPEAT_UNTIL_4: repeatSlot = 3; repeatEventName = L"Repeat Until #4"; break;
			}

			desc << L"Times: " << (int)times << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
			if (times == 0 && repeatCount[repeatSlot] == 0)
			{
				// infinite loop
				AddLoopForever(beginOffset, curOffset-beginOffset, repeatEventName);

				if (readMode == READMODE_ADD_TO_UI)
				{
					if (GetByte(curOffset) == 0x17)
					{
						AddEndOfTrack(curOffset, 1);
					}
				}
			}
			else
			{
				// regular N-times loop
				AddGenericEvent(beginOffset, curOffset-beginOffset, repeatEventName, desc.str().c_str(), CLR_LOOP, ICON_STARTREP);
			}

			if (repeatCount[repeatSlot] == 0)
			{
				repeatCount[repeatSlot] = times;
				curOffset = dest;
			}
			else
			{
				repeatCount[repeatSlot]--;
				if (repeatCount[repeatSlot] != 0)
				{
					curOffset = dest;
				}
			}

			break;
		}

		case EVENT_REPEAT_BREAK_1:
		case EVENT_REPEAT_BREAK_2:
		case EVENT_REPEAT_BREAK_3:
		case EVENT_REPEAT_BREAK_4:
		{
			uint8_t attributes = GetByte(curOffset++);
			uint16_t dest = GetShortBE(curOffset); curOffset += 2;

			uint8_t repeatSlot;
			wchar_t * repeatEventName;
			switch (eventType)
			{
			case EVENT_REPEAT_BREAK_1: repeatSlot = 0; repeatEventName = L"Repeat Break #1"; break;
			case EVENT_REPEAT_BREAK_2: repeatSlot = 1; repeatEventName = L"Repeat Break #2"; break;
			case EVENT_REPEAT_BREAK_3: repeatSlot = 2; repeatEventName = L"Repeat Break #3"; break;
			case EVENT_REPEAT_BREAK_4: repeatSlot = 3; repeatEventName = L"Repeat Break #4"; break;
			}

			desc << L"Note: { " << L"Triplet: " << (isNoteTriplet() ? L"On" : L"Off") << L"  " << L"Slur: " << (isNoteSlurred() ? L"On" : L"Off") << L"  " << L"2-Octave Up: " << (isNoteOctaveUp() ? L"On" : L"Off") << L" }  " << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
			AddGenericEvent(beginOffset, curOffset-beginOffset, repeatEventName, desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

			if (repeatCount[repeatSlot] == 1)
			{
				repeatCount[repeatSlot] = 0;
				noteAttributes &= ~(CAPCOM_SNES_MASK_NOTE_OCTAVE_UP | CAPCOM_SNES_MASK_NOTE_TRIPLET | CAPCOM_SNES_MASK_NOTE_SLURRED);
				noteAttributes |= attributes;
				curOffset = dest;
			}

			break;
		}

		case EVENT_GOTO:
		{
			uint16_t dest = GetShortBE(curOffset); curOffset += 2;
			desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
			uint32_t length = curOffset - beginOffset;

			if (!IsOffsetUsed(dest))
			{
				AddGenericEvent(beginOffset, length, L"Jump", desc.str().c_str(), CLR_LOOPFOREVER);
			}
			else
			{
				AddLoopForever(beginOffset, length, L"Jump");

				if (readMode == READMODE_ADD_TO_UI)
				{
					if (GetByte(curOffset) == 0x17)
					{
						AddEndOfTrack(curOffset, 1);
					}
				}
			}
			curOffset = dest;
			break;
		}

		case EVENT_END:
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			bContinue = false;
			break;

		case EVENT_PAN:
		{
			uint8_t newPan = GetByte(curOffset++) + 0x80; // signed -> unsigned
			uint8_t midiPan;

			if (parentSeq->version == V1_BGM_IN_LIST)
			{
				midiPan = newPan >> 1;
			}
			else
			{
				// use pan table (with linear interpolation)
				uint8_t panIndex = (newPan * 20) >> 8;
				uint8_t panRate = (newPan * 20) & 0xff;
				midiPan = CapcomSnesSeq::panTable[panIndex] + ((CapcomSnesSeq::panTable[panIndex + 1] - CapcomSnesSeq::panTable[panIndex]) * panRate >> 8);

			}
			midiPan = Convert7bitPercentPanValToStdMidiVal(midiPan);

			AddPan(beginOffset, curOffset-beginOffset, midiPan);
			break;
		}

		case EVENT_MASTER_VOLUME:
		{
			uint8_t newVolume = GetByte(curOffset++);
			uint8_t midiVolume;

			if (parentSeq->version == V1_BGM_IN_LIST)
			{
				// linear volume
				midiVolume = newVolume >> 1;
			}
			else
			{
				// use volume table (with linear interpolation)
				uint8_t volIndex = (newVolume * 16) >> 8;
				uint8_t volRate = (newVolume * 16) & 0xff;
				midiVolume = CapcomSnesSeq::volTable[volIndex] + ((CapcomSnesSeq::volTable[volIndex + 1] - CapcomSnesSeq::volTable[volIndex]) * volRate / 256);
			}
			midiVolume = Convert7bitPercentVolValToStdMidiVal(midiVolume >> 1);

			AddMasterVol(beginOffset, curOffset-beginOffset, newVolume);
			break;
		}

		case EVENT_LFO:
		{
			uint8_t lfoType = GetByte(curOffset++);
			uint8_t lfoAmount = GetByte(curOffset++);
			desc << L"Type: " << (int)lfoType << L"  Amount: " << (int)lfoAmount;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Param", desc.str().c_str(), CLR_LFO, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_ECHO_PARAM:
		{
			uint8_t echoArg1 = GetByte(curOffset++);
			uint8_t echoPreset = GetByte(curOffset++);
			desc << L"Arg1: " << (int)echoArg1 << L"  Preset: " << (int)echoPreset;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo Param", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_ECHO_ONOFF:
		{
			bool echoOn = (GetByte(curOffset++) & 1) != 0;
			EVENT_WITH_MIDITEXT_START
			if (echoOn)
			{
				AddReverb(beginOffset, curOffset-beginOffset, parentSeq->midiReverb, L"Echo On");
			}
			else
			{
				AddReverb(beginOffset, curOffset-beginOffset, 0, L"Echo Off");
			}
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_RELEASE_RATE:
		{
			uint8_t gain = GetByte(curOffset++) | 0xa0;
			desc << L"GAIN: $" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)gain;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Release Rate", desc.str().c_str(), CLR_SUSTAIN, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		default:
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
			EVENT_WITH_MIDITEXT_START
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
			EVENT_WITH_MIDITEXT_END
			pRoot->AddLogItem(new LogItem(wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, wstring(L"CapcomSnesSeq")));
			bContinue = false;
			break;
		}
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}

void CapcomSnesTrack::OnTickBegin(void)
{
}

void CapcomSnesTrack::OnTickEnd(void)
{
}
