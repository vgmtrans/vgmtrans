#include "stdafx.h"
#include "RareSnesSeq.h"
#include "RareSnesFormat.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

DECLARE_FORMAT(RareSnes);

//  **********
//  RareSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    32
#define SEQ_KEYOFS  36

const USHORT RareSnesSeq::NOTE_PITCH_TABLE[128] = {
	0x0000, 0x0040, 0x0044, 0x0048, 0x004c, 0x0051, 0x0055, 0x005b,
	0x0060, 0x0066, 0x006c, 0x0072, 0x0079, 0x0080, 0x0088, 0x0090,
	0x0098, 0x00a1, 0x00ab, 0x00b5, 0x00c0, 0x00cb, 0x00d7, 0x00e4,
	0x00f2, 0x0100, 0x010f, 0x011f, 0x0130, 0x0143, 0x0156, 0x016a,
	0x0180, 0x0196, 0x01af, 0x01c8, 0x01e3, 0x0200, 0x021e, 0x023f,
	0x0261, 0x0285, 0x02ab, 0x02d4, 0x02ff, 0x032d, 0x035d, 0x0390,
	0x03c7, 0x0400, 0x043d, 0x047d, 0x04c2, 0x050a, 0x0557, 0x05a8,
	0x05fe, 0x065a, 0x06ba, 0x0721, 0x078d, 0x0800, 0x087a, 0x08fb,
	0x0984, 0x0a14, 0x0aae, 0x0b50, 0x0bfd, 0x0cb3, 0x0d74, 0x0e41,
	0x0f1a, 0x1000, 0x10f4, 0x11f6, 0x1307, 0x1429, 0x155c, 0x16a1,
	0x17f9, 0x1966, 0x1ae9, 0x1c82, 0x1e34, 0x2000, 0x21e7, 0x23eb,
	0x260e, 0x2851, 0x2ab7, 0x2d41, 0x2ff2, 0x32cc, 0x35d1, 0x3904,
	0x3c68, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff,
	0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff,
	0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff,
	0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff
};

RareSnesSeq::RareSnesSeq(RawFile* file, RareSnesVersion ver, ULONG seqdataOffset, wstring newName)
: VGMSeq(RareSnesFormat::name, file, seqdataOffset), version(ver)
{
	name = newName;

	bAllowDiscontinuousTrackData = true;
	bWriteInitialTempo = true;
//	bLoadTrackOneByOne = false;

	LoadEventMap(this);
}

RareSnesSeq::~RareSnesSeq(void)
{
}

void RareSnesSeq::ResetVars(void)
{
	VGMSeq::ResetVars();

	midiReverb = 40;
	switch(version)
	{
	case DKC:
		timerFreq = 0x3c;
		break;
	default:
		timerFreq = 0x64;
		break;
	}
	tempo = initialTempo;
	tempoBPM = GetTempoInBPM();
}

int RareSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* seqHeader = AddHeader(dwOffset, MAX_TRACKS * 2 + 2, L"Sequence Header");
	ULONG curHeaderOffset = dwOffset;
	for (int i = 0; i < MAX_TRACKS; i++)
	{
		USHORT trkOff = GetShort(curHeaderOffset);
		seqHeader->AddPointer(curHeaderOffset, 2, trkOff, (trkOff != 0), L"Track Pointer");
		curHeaderOffset += 2;
	}
	initialTempo = GetByte(curHeaderOffset);
	seqHeader->AddTempo(curHeaderOffset++, 1, L"Tempo");
	seqHeader->AddUnknownItem(curHeaderOffset++, 1);

	return true;		//successful
}


int RareSnesSeq::GetTrackPointers(void)
{
	for (int i = 0; i < MAX_TRACKS; i++)
	{
		USHORT trkOff = GetShort(dwOffset + i * 2);
		if (trkOff != 0)
			aTracks.push_back(new RareSnesTrack(this, trkOff));
	}
	return true;
}

void RareSnesSeq::LoadEventMap(RareSnesSeq *pSeqFile)
{
	// common events
	pSeqFile->EventMap[0x00] = EVENT_END;
	pSeqFile->EventMap[0x01] = EVENT_PROGCHANGE;
	pSeqFile->EventMap[0x02] = EVENT_VOLLR;
	pSeqFile->EventMap[0x03] = EVENT_GOTO;
	pSeqFile->EventMap[0x04] = EVENT_CALLNTIMES;
	pSeqFile->EventMap[0x05] = EVENT_RET;
	pSeqFile->EventMap[0x06] = EVENT_DEFDURON;
	pSeqFile->EventMap[0x07] = EVENT_DEFDUROFF;
	pSeqFile->EventMap[0x08] = EVENT_PITCHSLIDEUP;
	pSeqFile->EventMap[0x09] = EVENT_PITCHSLIDEDOWN;
	pSeqFile->EventMap[0x0a] = EVENT_PITCHSLIDEOFF;
	pSeqFile->EventMap[0x0b] = EVENT_TEMPO;
	pSeqFile->EventMap[0x0c] = EVENT_TEMPOADD;
	pSeqFile->EventMap[0x0d] = EVENT_VIBRATOSHORT;
	pSeqFile->EventMap[0x0e] = EVENT_VIBRATOOFF;
	pSeqFile->EventMap[0x0f] = EVENT_VIBRATO;
	pSeqFile->EventMap[0x10] = EVENT_ADSR;
	pSeqFile->EventMap[0x11] = EVENT_MASTVOLLR;
	pSeqFile->EventMap[0x12] = EVENT_TUNING;
	pSeqFile->EventMap[0x13] = EVENT_TRANSPABS;
	pSeqFile->EventMap[0x14] = EVENT_TRANSPREL;
	pSeqFile->EventMap[0x15] = EVENT_ECHOPARAM;
	pSeqFile->EventMap[0x16] = EVENT_ECHOON;
	pSeqFile->EventMap[0x17] = EVENT_ECHOOFF;
	pSeqFile->EventMap[0x18] = EVENT_ECHOFIR;
	pSeqFile->EventMap[0x19] = EVENT_NOISECLK;
	pSeqFile->EventMap[0x1a] = EVENT_NOISEON;
	pSeqFile->EventMap[0x1b] = EVENT_NOISEOFF;
	pSeqFile->EventMap[0x1c] = EVENT_SETALTNOTE1;
	pSeqFile->EventMap[0x1d] = EVENT_SETALTNOTE2;
	pSeqFile->EventMap[0x26] = EVENT_PITCHSLIDEDOWNSHORT;
	pSeqFile->EventMap[0x27] = EVENT_PITCHSLIDEUPSHORT;
	pSeqFile->EventMap[0x2b] = EVENT_LONGDURON;
	pSeqFile->EventMap[0x2c] = EVENT_LONGDUROFF;

	switch(pSeqFile->version)
	{
	case DKC:
		pSeqFile->EventMap[0x1c] = EVENT_SETVOLADSRPRESET1;
		pSeqFile->EventMap[0x1d] = EVENT_SETVOLADSRPRESET2;
		pSeqFile->EventMap[0x1e] = EVENT_SETVOLADSRPRESET3;
		pSeqFile->EventMap[0x1f] = EVENT_SETVOLADSRPRESET4;
		pSeqFile->EventMap[0x20] = EVENT_SETVOLADSRPRESET5;
		pSeqFile->EventMap[0x21] = EVENT_GETVOLADSRPRESET1;
		pSeqFile->EventMap[0x22] = EVENT_GETVOLADSRPRESET2;
		pSeqFile->EventMap[0x23] = EVENT_GETVOLADSRPRESET3;
		pSeqFile->EventMap[0x24] = EVENT_GETVOLADSRPRESET4;
		pSeqFile->EventMap[0x25] = EVENT_GETVOLADSRPRESET5;
		pSeqFile->EventMap[0x28] = EVENT_PROGCHANGEVOL;
		pSeqFile->EventMap[0x29] = EVENT_UNKNOWN1;
		pSeqFile->EventMap[0x2a] = EVENT_TIMERFREQ;
		pSeqFile->EventMap[0x2d] = EVENT_CONDJUMP;
		pSeqFile->EventMap[0x2e] = EVENT_SETCONDJUMPPARAM;
		pSeqFile->EventMap[0x2f] = EVENT_TREMOLO;
		pSeqFile->EventMap[0x30] = EVENT_TREMOLOOFF;
		break;

	case KI:
		//removed common events
		pSeqFile->EventMap.erase(0x0c);
		pSeqFile->EventMap.erase(0x0d);
		pSeqFile->EventMap.erase(0x11);
		pSeqFile->EventMap.erase(0x15);
		pSeqFile->EventMap.erase(0x18);
		pSeqFile->EventMap.erase(0x19);
		pSeqFile->EventMap.erase(0x1a);
		pSeqFile->EventMap.erase(0x1b);
		pSeqFile->EventMap.erase(0x1c);
		pSeqFile->EventMap.erase(0x1d);

		pSeqFile->EventMap[0x1e] = EVENT_VOLCENTER;
		pSeqFile->EventMap[0x1f] = EVENT_CALLONCE;
		pSeqFile->EventMap[0x20] = EVENT_RESETADSR;
		pSeqFile->EventMap[0x21] = EVENT_RESETADSRSOFT;
		pSeqFile->EventMap[0x22] = EVENT_VOICEPARAMSHORT;
		pSeqFile->EventMap[0x23] = EVENT_ECHODELAY;
		//pSeqFile->EventMap[0x24] = null;
		//pSeqFile->EventMap[0x25] = null;
		//pSeqFile->EventMap[0x28] = null;
		//pSeqFile->EventMap[0x29] = null;
		//pSeqFile->EventMap[0x2a] = null;
		//pSeqFile->EventMap[0x2d] = null;
		//pSeqFile->EventMap[0x2e] = null;
		//pSeqFile->EventMap[0x2f] = null;
		//pSeqFile->EventMap[0x30] = null;
		break;

	case DKC2:
		//removed common events
		pSeqFile->EventMap.erase(0x11);

		pSeqFile->EventMap[0x1e] = EVENT_SETVOLPRESETS;
		pSeqFile->EventMap[0x1f] = EVENT_ECHODELAY;
		pSeqFile->EventMap[0x20] = EVENT_GETVOLPRESET1;
		pSeqFile->EventMap[0x21] = EVENT_CALLONCE;
		pSeqFile->EventMap[0x22] = EVENT_VOICEPARAM;
		pSeqFile->EventMap[0x23] = EVENT_VOLCENTER;
		pSeqFile->EventMap[0x24] = EVENT_MASTVOL;
		//pSeqFile->EventMap[0x25] = null;
		//pSeqFile->EventMap[0x28] = null;
		//pSeqFile->EventMap[0x29] = null;
		//pSeqFile->EventMap[0x2a] = null;
		//pSeqFile->EventMap[0x2d] = null;
		//pSeqFile->EventMap[0x2e] = null;
		//pSeqFile->EventMap[0x2f] = null;
		pSeqFile->EventMap[0x30] = EVENT_ECHOOFF; // duplicated
		pSeqFile->EventMap[0x31] = EVENT_GETVOLPRESET2;
		pSeqFile->EventMap[0x32] = EVENT_ECHOOFF; // duplicated
		break;

	case WNRN:
		//removed common events
		pSeqFile->EventMap.erase(0x19);
		pSeqFile->EventMap.erase(0x1a);
		pSeqFile->EventMap.erase(0x1b);

		//pSeqFile->EventMap[0x1e] = null;
		//pSeqFile->EventMap[0x1f] = null;
		pSeqFile->EventMap[0x20] = EVENT_MASTVOL;
		pSeqFile->EventMap[0x21] = EVENT_VOLCENTER;
		pSeqFile->EventMap[0x22] = EVENT_UNKNOWN3;
		pSeqFile->EventMap[0x23] = EVENT_CALLONCE;
		pSeqFile->EventMap[0x24] = EVENT_LFOOFF;
		pSeqFile->EventMap[0x25] = EVENT_UNKNOWN4;
		pSeqFile->EventMap[0x28] = EVENT_PROGCHANGEVOL;
		pSeqFile->EventMap[0x29] = EVENT_UNKNOWN1;
		pSeqFile->EventMap[0x2a] = EVENT_TIMERFREQ;
		//pSeqFile->EventMap[0x2d] = null;
		//pSeqFile->EventMap[0x2e] = null;
		pSeqFile->EventMap[0x2f] = EVENT_TREMOLO;
		pSeqFile->EventMap[0x30] = EVENT_TREMOLOOFF;
		//pSeqFile->EventMap[0x31] = EVENT_RESET;
		break;
	}
}

double RareSnesSeq::GetTempoInBPM ()
{
	return GetTempoInBPM(tempo, timerFreq);
}

double RareSnesSeq::GetTempoInBPM (BYTE tempo)
{
	return GetTempoInBPM(tempo, timerFreq);
}

double RareSnesSeq::GetTempoInBPM (BYTE tempo, BYTE timerFreq)
{
	return (double) 60000000 / (SEQ_PPQN * (125 * timerFreq)) * ((double) tempo / 256);
}


//  ************
//  RareSnesTrack
//  ************

RareSnesTrack::RareSnesTrack(RareSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = true;
}

void RareSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	cKeyCorrection = SEQ_KEYOFS;

	rptNestLevel = 0;
	spcNotePitch = 0;
	spcTranspose = 0;
	spcTransposeAbs = 0;
	spcTuning = 0;
	defNoteDur = 0;
	useLongDur = false;
	altNoteByte1 = altNoteByte2 = 0x81;
}


void RareSnesTrack::CalcVolPanFromVolLR(BYTE volLByte, BYTE volRByte, BYTE& midiVol, BYTE& midiPan)
{
	double volL = (volLByte & 0x7f) / 127.0;
	double volR = (volRByte & 0x7f) / 127.0;
	double vol;
	double pan;

	// both input and output are linear in this function
	vol = (volL + volR) / 2;
	midiVol = (BYTE)(vol * 127 + 0.5);
	if (vol != 0)
	{
		pan = volR / (volL + volR);
		midiPan = (BYTE)(pan * 128 + 0.5);
		if (midiPan == 128)
		{
			midiPan = 127;
		}
	}
}

int RareSnesTrack::ReadEvent(void)
{
	RareSnesSeq* parentSeq = (RareSnesSeq*)this->parentSeq;
	ULONG beginOffset = curOffset;
	if (curOffset >= 0x10000)
	{
		return false;
	}

	BYTE statusByte = GetByte(curOffset++);
	BYTE newMidiVol, newMidiPan;
	bool bContinue = true;

	wstringstream desc;

	if (statusByte >= 0x80)
	{
		BYTE noteByte = statusByte;

		// check for "reuse last key"
		if (noteByte == 0xe1)
		{
			noteByte = altNoteByte2;
		}
		else if (noteByte >= 0xe0)
		{
			noteByte = altNoteByte1;
		}

		BYTE key = noteByte - 0x81;
		BYTE spcKey = min(max(key + spcTranspose, 0), 0x7f);

		USHORT dur;
		if (defNoteDur != 0)
		{
			dur = defNoteDur;
		}
		else
		{
			if (useLongDur)
			{
				dur = GetShortBE(curOffset);
				curOffset += 2;
			}
			else
			{
				dur = GetByte(curOffset++);
			}
		}

		if (noteByte == 0x80)
		{
			//wostringstream ssTrace;
			//ssTrace << L"Rest: " << dur << L" " << defNoteDur << L" " << (useLongDur ? L"L" : L"S") << std::endl;
			//OutputDebugString(ssTrace.str().c_str());

			AddRest(beginOffset, curOffset-beginOffset, dur);
		}
		else
		{
			spcNotePitch = RareSnesSeq::NOTE_PITCH_TABLE[spcKey]; // TODO: +tuning to the pitch?

			//wostringstream ssTrace;
			//ssTrace << L"Note: " << key << L" " << dur << L" " << defNoteDur << L" " << (useLongDur ? L"L" : L"S") << L" P=" << spcNotePitch << std::endl;
			//OutputDebugString(ssTrace.str().c_str());

			BYTE vel = 127;
			AddNoteByDur(beginOffset, curOffset-beginOffset, key, vel, dur);
			AddDelta(dur);
		}
	}
	else
	{
		RareSnesSeqEventType eventType = (RareSnesSeqEventType)0;
		map<BYTE, RareSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
		if (pEventType != parentSeq->EventMap.end())
		{
			eventType = pEventType->second;
		}

		switch (eventType)
		{
		case EVENT_UNKNOWN0:
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event");
			break;

		case EVENT_UNKNOWN1:
		{
			BYTE arg1 = GetByte(curOffset++);
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
				<< std::dec << std::setfill(L' ') << std::setw(0)
				<< L"  Arg1: " << (int)arg1;
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event");
			break;
		}

		case EVENT_UNKNOWN2:
		{
			BYTE arg1 = GetByte(curOffset++);
			BYTE arg2 = GetByte(curOffset++);
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
				<< std::dec << std::setfill(L' ') << std::setw(0)
				<< L"  Arg1: " << (int)arg1
				<< L"  Arg2: " << (int)arg2;
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event");
			break;
		}

		case EVENT_UNKNOWN3:
		{
			BYTE arg1 = GetByte(curOffset++);
			BYTE arg2 = GetByte(curOffset++);
			BYTE arg3 = GetByte(curOffset++);
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
				<< std::dec << std::setfill(L' ') << std::setw(0)
				<< L"  Arg1: " << (int)arg1
				<< L"  Arg2: " << (int)arg2
				<< L"  Arg3: " << (int)arg3;
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event");
			break;
		}

		case EVENT_UNKNOWN4:
		{
			BYTE arg1 = GetByte(curOffset++);
			BYTE arg2 = GetByte(curOffset++);
			BYTE arg3 = GetByte(curOffset++);
			BYTE arg4 = GetByte(curOffset++);
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte
				<< std::dec << std::setfill(L' ') << std::setw(0)
				<< L"  Arg1: " << (int)arg1
				<< L"  Arg2: " << (int)arg2
				<< L"  Arg3: " << (int)arg3
				<< L"  Arg4: " << (int)arg4;
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event");
			break;
		}

		case EVENT_END:
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			bContinue = false;
			//loaded = true;
			break;

		case EVENT_PROGCHANGE:
		{
			BYTE newProg = GetByte(curOffset++);
			AddProgramChange(beginOffset, curOffset-beginOffset, newProg, true);
			break;
		}

		case EVENT_PROGCHANGEVOL:
		{
			BYTE newProg = GetByte(curOffset++);
			BYTE newVolL = GetByte(curOffset++);
			BYTE newVolR = GetByte(curOffset++);

			spcVolL = newVolL;
			spcVolR = newVolR;
			AddProgramChange(beginOffset, curOffset-beginOffset, newProg, true, L"Program Change, Volume");
			AddVolLRNoItem(spcVolL, spcVolR);
			break;
		}

		case EVENT_VOLLR:
		{
			BYTE newVolL = GetByte(curOffset++);
			BYTE newVolR = GetByte(curOffset++);

			spcVolL = newVolL;
			spcVolR = newVolR;
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Volume L/R");
			break;
		}

		case EVENT_VOLCENTER:
		{
			BYTE newVol = GetByte(curOffset++);

			spcVolL = newVol;
			spcVolR = newVol;
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Volume");
			break;
		}

		case EVENT_GOTO:
		{
			USHORT dest = GetShort(curOffset); curOffset += 2;
			desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
			AddLoopForever(beginOffset, curOffset-beginOffset, L"Jump");

			curOffset = dest;
			if (IsOffsetUsed(curOffset))
			{
				//loaded = true;
				bContinue = false;
			}
			break;
		}

		case EVENT_CALLNTIMES:
		{
			BYTE times = GetByte(curOffset++);
			USHORT dest = GetShort(curOffset); curOffset += 2;

			desc << L"Times: " << (int)times << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
			AddGenericEvent(beginOffset, curOffset-beginOffset, (times == 1 ? L"Pattern Play" : L"Pattern Repeat"), CLR_LOOP, ICON_STARTREP);

			if (rptNestLevel == RARESNES_RPTNESTMAX)
			{
				OutputDebugString(L"RareSnesSeq: subroutine nest level overflow\n");
				bContinue = false;
				break;
			}

			rptRetnAddr[rptNestLevel] = curOffset;
			rptCount[rptNestLevel] = times;
			rptStart[rptNestLevel] = dest;
			rptNestLevel++;
			curOffset = dest;
			break;
		}

		case EVENT_CALLONCE:
		{
			USHORT dest = GetShort(curOffset); curOffset += 2;

			desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pattern Play", CLR_LOOP, ICON_STARTREP);

			if (rptNestLevel == RARESNES_RPTNESTMAX)
			{
				OutputDebugString(L"RareSnesSeq: subroutine nest level overflow\n");
				bContinue = false;
				break;
			}

			rptRetnAddr[rptNestLevel] = curOffset;
			rptCount[rptNestLevel] = 1;
			rptStart[rptNestLevel] = dest;
			rptNestLevel++;
			curOffset = dest;
			break;
		}

		case EVENT_RET:
		{
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"End Pattern", CLR_TRACKEND, ICON_ENDREP);

			if (rptNestLevel == 0)
			{
				OutputDebugString(L"RareSnesSeq: subroutine nest level overflow\n");
				bContinue = false;
				break;
			}

			rptNestLevel--;
			rptCount[rptNestLevel] = (rptCount[rptNestLevel] - 1) & 0xff;
			if (rptCount[rptNestLevel] != 0) {
				// continue
				curOffset = rptStart[rptNestLevel];
				rptNestLevel++;
			}
			else {
				// return
				curOffset = rptRetnAddr[rptNestLevel];
			}
			break;
		}

		case EVENT_DEFDURON:
		{
			if (useLongDur)
			{
				defNoteDur = GetShortBE(curOffset);
				curOffset += 2;
			}
			else
			{
				defNoteDur = GetByte(curOffset++);
			}

			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Default Duration On", CLR_DURNOTE, ICON_NOTE);
			break;
		}

		case EVENT_DEFDUROFF:
			defNoteDur = 0;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Default Duration Off", CLR_DURNOTE, ICON_NOTE);
			break;

		case EVENT_PITCHSLIDEUP:
		{
			curOffset += 5;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide Up", CLR_PITCHBEND, ICON_CONTROL);
			break;
		}

		case EVENT_PITCHSLIDEDOWN:
		{
			curOffset += 5;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide Down", CLR_PITCHBEND, ICON_CONTROL);
			break;
		}

		case EVENT_PITCHSLIDEOFF:
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide Off", CLR_PITCHBEND, ICON_CONTROL);
			break;

		case EVENT_TEMPO:
		{
			BYTE newTempo = GetByte(curOffset++);
			parentSeq->tempo = newTempo;
			AddTempoBPM(beginOffset, curOffset-beginOffset, parentSeq->GetTempoInBPM());
			break;
		}

		case EVENT_TEMPOADD:
		{
			S8 deltaTempo = (signed) GetByte(curOffset++);
			parentSeq->tempo = (parentSeq->tempo + deltaTempo) & 0xff;
			AddTempoBPM(beginOffset, curOffset-beginOffset, parentSeq->GetTempoInBPM(), L"Tempo Add");
			break;
		}

		case EVENT_VIBRATOSHORT:
		{
			curOffset += 3;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato (Short)", CLR_MODULATION, ICON_CONTROL);
			break;
		}

		case EVENT_VIBRATOOFF:
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato Off", CLR_MODULATION, ICON_CONTROL);
			break;

		case EVENT_VIBRATO:
		{
			curOffset += 4;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato", CLR_MODULATION, ICON_CONTROL);
			break;
		}

		case EVENT_TREMOLOOFF:
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tremolo Off", CLR_MODULATION, ICON_CONTROL);
			break;

		case EVENT_TREMOLO:
		{
			curOffset += 4;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tremolo", CLR_MODULATION, ICON_CONTROL);
			break;
		}

		case EVENT_ADSR:
		{
			USHORT newADSR = GetShortBE(curOffset); curOffset += 2;
			desc << L"ADSR: " << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)newADSR;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR", CLR_ADSR, ICON_CONTROL);
			break;
		}

		case EVENT_MASTVOL:
		{
			BYTE newVol = GetByte(curOffset++);
			AddMasterVol(beginOffset, curOffset-beginOffset, newVol & 0x7f);
			break;
		}

		case EVENT_MASTVOLLR:
		{
			BYTE newVolL = GetByte(curOffset++);
			BYTE newVolR = GetByte(curOffset++);
			BYTE newVol = ((newVolL & 0x7f) + (newVolR & 0x7f)) / 2; // workaround: convert to mono
			AddMasterVol(beginOffset, curOffset-beginOffset, newVol, L"Master Volume L/R");
			break;
		}

		case EVENT_TUNING:
		{
			S8 newTuning = (signed) GetByte(curOffset++);
			spcTuning = newTuning;
			desc << L"Tuning: " << (int)newTuning;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tuning", CLR_PITCHBEND, ICON_CONTROL);
			break;
		}

		case EVENT_TRANSPABS: // should be used for pitch correction of instrument
		{
			S8 newTransp = GetByte(curOffset++);
			spcTranspose = spcTransposeAbs = newTransp;
			//AddTranspose(beginOffset, curOffset-beginOffset, 0, L"Transpose (Abs)");

			// add event without MIDI event
			if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(beginOffset))
				AddEvent(new TransposeSeqEvent(this, newTransp, beginOffset, curOffset-beginOffset, L"Transpose (Abs)"));

			cKeyCorrection = SEQ_KEYOFS;
			break;
		}

		case EVENT_TRANSPREL:
		{
			S8 deltaTransp = (signed) GetByte(curOffset++);
			spcTranspose = (spcTranspose + deltaTransp) & 0xff;
			//AddTranspose(beginOffset, curOffset-beginOffset, spcTransposeAbs - spcTranspose, L"Transpose (Rel)");

			// add event without MIDI event
			if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(beginOffset))
				AddEvent(new TransposeSeqEvent(this, deltaTransp, beginOffset, curOffset-beginOffset, L"Transpose (Rel)"));

			cKeyCorrection += deltaTransp;
			break;
		}

		case EVENT_ECHOPARAM:
		{
			BYTE newFeedback = GetByte(curOffset++);
			BYTE newVolL = GetByte(curOffset++);
			BYTE newVolR = GetByte(curOffset++);
			parentSeq->midiReverb = ((newVolL & 0x7f) + (newVolR & 0x7f)) / 2;
			// TODO: update MIDI reverb value for each tracks?

			desc << L"Feedback: " << (int)newFeedback << L"  Volume: " << (int)newVolL << L", " << (int)newVolR;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo Param", CLR_REVERB, ICON_CONTROL);
			break;
		}

		case EVENT_ECHOON:
			AddReverb(beginOffset, curOffset-beginOffset, parentSeq->midiReverb, L"Echo On");
			break;

		case EVENT_ECHOOFF:
			AddReverb(beginOffset, curOffset-beginOffset, 0, L"Echo Off");
			break;

		case EVENT_ECHOFIR:
		{
			BYTE newFIR[8];
			GetBytes(curOffset, 8, newFIR);
			curOffset += 8;

			desc << L"Filter: " << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase;
			for (int iFIRIndex = 0; iFIRIndex < 8; iFIRIndex++)
			{
				desc << (int)newFIR[iFIRIndex];
			}

			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo FIR", CLR_REVERB, ICON_CONTROL);
			break;
		}

		case EVENT_NOISECLK:
		{
			BYTE newCLK = GetByte(curOffset++);
			desc << L"CLK: " << (int)newCLK;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Noise Frequency", CLR_CHANGESTATE, ICON_CONTROL);
			break;
		}

		case EVENT_NOISEON:
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Noise On", CLR_CHANGESTATE, ICON_CONTROL);
			break;

		case EVENT_NOISEOFF:
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Noise Off", CLR_CHANGESTATE, ICON_CONTROL);
			break;

		case EVENT_SETALTNOTE1:
			altNoteByte1 = GetByte(curOffset++);
			desc << L"Note: " << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)altNoteByte1;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Set Alt Note 1", CLR_CHANGESTATE, ICON_NOTE);
			break;

		case EVENT_SETALTNOTE2:
			altNoteByte2 = GetByte(curOffset++);
			desc << L"Note: " << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)altNoteByte2;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Set Alt Note 2", CLR_CHANGESTATE, ICON_NOTE);
			break;

		case EVENT_PITCHSLIDEDOWNSHORT:
		{
			curOffset += 4;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide Down (Short)", CLR_PITCHBEND, ICON_CONTROL);
			break;
		}

		case EVENT_PITCHSLIDEUPSHORT:
		{
			curOffset += 4;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide Up (Short)", CLR_PITCHBEND, ICON_CONTROL);
			break;
		}

		case EVENT_LONGDURON:
			useLongDur = true;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Long Duration On", CLR_DURNOTE, ICON_NOTE);
			break;

		case EVENT_LONGDUROFF:
			useLongDur = false;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Long Duration Off", CLR_DURNOTE, ICON_NOTE);
			break;

		case EVENT_SETVOLADSRPRESET1:
		{
			BYTE newVolL = GetByte(curOffset++);
			BYTE newVolR = GetByte(curOffset++);
			USHORT newADSR = GetShortBE(curOffset); curOffset += 2;

			parentSeq->presetVolL[0] = newVolL;
			parentSeq->presetVolR[0] = newVolR;
			parentSeq->presetADSR[0] = newADSR;

			// add event without MIDI events
			CalcVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);
			if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(beginOffset))
				AddEvent(new VolSeqEvent(this, newMidiVol, beginOffset, curOffset-beginOffset, L"Set Vol/ADSR Preset 1"));
			break;
		}

		case EVENT_SETVOLADSRPRESET2:
		{
			BYTE newVolL = GetByte(curOffset++);
			BYTE newVolR = GetByte(curOffset++);
			USHORT newADSR = GetShortBE(curOffset); curOffset += 2;

			parentSeq->presetVolL[1] = newVolL;
			parentSeq->presetVolR[1] = newVolR;
			parentSeq->presetADSR[1] = newADSR;

			// add event without MIDI events
			CalcVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);
			if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(beginOffset))
				AddEvent(new VolSeqEvent(this, newMidiVol, beginOffset, curOffset-beginOffset, L"Set Vol/ADSR Preset 2"));
			break;
		}

		case EVENT_SETVOLADSRPRESET3:
		{
			BYTE newVolL = GetByte(curOffset++);
			BYTE newVolR = GetByte(curOffset++);
			USHORT newADSR = GetShortBE(curOffset); curOffset += 2;

			parentSeq->presetVolL[2] = newVolL;
			parentSeq->presetVolR[2] = newVolR;
			parentSeq->presetADSR[2] = newADSR;

			// add event without MIDI events
			CalcVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);
			if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(beginOffset))
				AddEvent(new VolSeqEvent(this, newMidiVol, beginOffset, curOffset-beginOffset, L"Set Vol/ADSR Preset 3"));
			break;
		}

		case EVENT_SETVOLADSRPRESET4:
		{
			BYTE newVolL = GetByte(curOffset++);
			BYTE newVolR = GetByte(curOffset++);
			USHORT newADSR = GetShortBE(curOffset); curOffset += 2;

			parentSeq->presetVolL[3] = newVolL;
			parentSeq->presetVolR[3] = newVolR;
			parentSeq->presetADSR[3] = newADSR;

			// add event without MIDI events
			CalcVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);
			if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(beginOffset))
				AddEvent(new VolSeqEvent(this, newMidiVol, beginOffset, curOffset-beginOffset, L"Set Vol/ADSR Preset 4"));
			break;
		}

		case EVENT_SETVOLADSRPRESET5:
		{
			BYTE newVolL = GetByte(curOffset++);
			BYTE newVolR = GetByte(curOffset++);
			USHORT newADSR = GetShortBE(curOffset); curOffset += 2;

			parentSeq->presetVolL[4] = newVolL;
			parentSeq->presetVolR[4] = newVolR;
			parentSeq->presetADSR[4] = newADSR;

			// add event without MIDI events
			CalcVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);
			if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(beginOffset))
				AddEvent(new VolSeqEvent(this, newMidiVol, beginOffset, curOffset-beginOffset, L"Set Vol/ADSR Preset 5"));
			break;
		}

		case EVENT_GETVOLADSRPRESET1:
			spcVolL = parentSeq->presetVolL[0];
			spcVolR = parentSeq->presetVolR[0];
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Vol/ADSR Preset 1");
			break;

		case EVENT_GETVOLADSRPRESET2:
			spcVolL = parentSeq->presetVolL[1];
			spcVolR = parentSeq->presetVolR[1];
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Vol/ADSR Preset 2");
			break;

		case EVENT_GETVOLADSRPRESET3:
			spcVolL = parentSeq->presetVolL[2];
			spcVolR = parentSeq->presetVolR[2];
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Vol/ADSR Preset 3");
			break;

		case EVENT_GETVOLADSRPRESET4:
			spcVolL = parentSeq->presetVolL[3];
			spcVolR = parentSeq->presetVolR[3];
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Vol/ADSR Preset 4");
			break;

		case EVENT_GETVOLADSRPRESET5:
			spcVolL = parentSeq->presetVolL[4];
			spcVolR = parentSeq->presetVolR[4];
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Vol/ADSR Preset 5");
			break;

		case EVENT_TIMERFREQ:
		{
			BYTE newFreq = GetByte(curOffset++);
			parentSeq->timerFreq = newFreq;
			AddTempoBPM(beginOffset, curOffset-beginOffset, parentSeq->GetTempoInBPM(), L"Timer Frequency");
			break;
		}

		//case EVENT_CONDJUMP:
		//	break;

		//case EVENT_SETCONDJUMPPARAM:
		//	break;

		case EVENT_RESETADSR:
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reset ADSR", CLR_ADSR, ICON_CONTROL/*, L"ADSR: 8FE0"*/);
			break;

		case EVENT_RESETADSRSOFT:
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reset ADSR (Soft)", CLR_ADSR, ICON_CONTROL/*, L"ADSR: 8EE0"*/);
			break;

		case EVENT_VOICEPARAMSHORT:
		{
			BYTE newProg = GetByte(curOffset++);
			S8 newTransp = GetByte(curOffset++);
			S8 newTuning = GetByte(curOffset++);

			desc << L"Program Number: " << (int)newProg << L"  Transpose: " << (int)newTransp << L"  Tuning: " << (int)newTuning;

			// instrument
			AddProgramChange(beginOffset, curOffset-beginOffset, newProg, true, L"Program Change, Transpose, Tuning");

			// transpose
			spcTranspose = spcTransposeAbs = newTransp;
			cKeyCorrection = SEQ_KEYOFS;

			// tuning
			spcTuning = newTuning;
			break;
		}

		case EVENT_VOICEPARAM:
		{
			BYTE newProg = GetByte(curOffset++);
			S8 newTransp = GetByte(curOffset++);
			S8 newTuning = GetByte(curOffset++);
			BYTE newVolL = GetByte(curOffset++);
			BYTE newVolR = GetByte(curOffset++);
			USHORT newADSR = GetShortBE(curOffset); curOffset += 2;

			desc << L"Program Number: " << (int)newProg << L"  Transpose: " << (int)newTransp << L"  Tuning: " << (int)newTuning;
			desc << L"  Volume: " << (int)newVolL << L", " << (int)newVolR;
			desc << L"  ADSR: " << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)newADSR;

			// instrument
			AddProgramChange(beginOffset, curOffset-beginOffset, newProg, true, L"Program Change, Transpose, Tuning, Volume L/R, ADSR");

			// transpose
			spcTranspose = spcTransposeAbs = newTransp;
			cKeyCorrection = SEQ_KEYOFS;

			// tuning
			spcTuning = newTuning;

			// volume
			spcVolL = newVolL;
			spcVolR = newVolR;
			AddVolLRNoItem(spcVolL, spcVolR);

			// ADSR

			break;
		}

		case EVENT_ECHODELAY:
		{
			BYTE newEDL = GetByte(curOffset++);
			desc << L"Delay: " << (int)newEDL;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo Delay", CLR_REVERB, ICON_CONTROL);
			break;
		}

		case EVENT_SETVOLPRESETS:
		{
			BYTE newVolL1 = GetByte(curOffset++);
			BYTE newVolR1 = GetByte(curOffset++);
			BYTE newVolL2 = GetByte(curOffset++);
			BYTE newVolR2 = GetByte(curOffset++);

			parentSeq->presetVolL[0] = newVolL1;
			parentSeq->presetVolR[0] = newVolR1;
			parentSeq->presetVolL[1] = newVolL2;
			parentSeq->presetVolR[1] = newVolR2;

			// add event without MIDI events
			CalcVolPanFromVolLR(parentSeq->presetVolL[0], parentSeq->presetVolR[0], newMidiVol, newMidiPan);
			if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(beginOffset))
				AddEvent(new VolSeqEvent(this, newMidiVol, beginOffset, curOffset-beginOffset, L"Set Volume Preset"));
			break;
		}

		case EVENT_GETVOLPRESET1:
			spcVolL = parentSeq->presetVolL[0];
			spcVolR = parentSeq->presetVolR[0];
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Volume Preset 1");
			break;

		case EVENT_GETVOLPRESET2:
			spcVolL = parentSeq->presetVolL[1];
			spcVolR = parentSeq->presetVolR[1];
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Volume Preset 2");
			break;

		case EVENT_LFOOFF:
			if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(beginOffset))
				AddEvent(new ModulationSeqEvent(this, 0, beginOffset, curOffset-beginOffset, L"Pitch Slide/Vibrato/Tremolo Off"));
			break;

		default:
			desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)statusByte;
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event");
			bContinue = false;
			break;
		}
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}

void RareSnesTrack::AddVolLR(ULONG offset, ULONG length, BYTE spcVolL, BYTE spcVolR, const wchar_t* sEventName)
{
	BYTE newMidiVol;
	BYTE newMidiPan;
	CalcVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);

	if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(offset))
		AddEvent(new VolSeqEvent(this, newMidiVol, offset, length, sEventName));

	// add MIDI events only if updated
	if (newMidiVol != vol)
	{
		AddVolNoItem(newMidiVol);
	}
	if (newMidiVol != 0 && newMidiPan != prevPan)
	{
		AddPanNoItem(newMidiPan);
	}
}

void RareSnesTrack::AddVolLRNoItem(BYTE spcVolL, BYTE spcVolR)
{
	BYTE newMidiVol;
	BYTE newMidiPan;
	CalcVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);

	// add MIDI events only if updated
	if (newMidiVol != vol)
	{
		AddVolNoItem(newMidiVol);
	}
	if (newMidiVol != 0 && newMidiPan != prevPan)
	{
		AddPanNoItem(newMidiPan);
	}
}
