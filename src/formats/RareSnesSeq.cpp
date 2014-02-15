#include "stdafx.h"
#include "RareSnesSeq.h"
#include "RareSnesFormat.h"
#include "ScaleConversion.h"

using namespace std;

DECLARE_FORMAT(RareSnes);

//  **********
//  RareSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    32
#define SEQ_KEYOFS  36

const uint16_t RareSnesSeq::NOTE_PITCH_TABLE[128] = {
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

RareSnesSeq::RareSnesSeq(RawFile* file, RareSnesVersion ver, uint32_t seqdataOffset, wstring newName)
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

bool RareSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(SEQ_PPQN);

	VGMHeader* seqHeader = AddHeader(dwOffset, MAX_TRACKS * 2 + 2, L"Sequence Header");
	uint32_t curHeaderOffset = dwOffset;
	for (int i = 0; i < MAX_TRACKS; i++)
	{
		uint16_t trkOff = GetShort(curHeaderOffset);
		seqHeader->AddPointer(curHeaderOffset, 2, trkOff, (trkOff != 0), L"Track Pointer");
		curHeaderOffset += 2;
	}
	initialTempo = GetByte(curHeaderOffset);
	seqHeader->AddTempo(curHeaderOffset++, 1, L"Tempo");
	seqHeader->AddUnknownItem(curHeaderOffset++, 1);

	return true;		//successful
}


bool RareSnesSeq::GetTrackPointers(void)
{
	for (int i = 0; i < MAX_TRACKS; i++)
	{
		uint16_t trkOff = GetShort(dwOffset + i * 2);
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

double RareSnesSeq::GetTempoInBPM (uint8_t tempo)
{
	return GetTempoInBPM(tempo, timerFreq);
}

double RareSnesSeq::GetTempoInBPM (uint8_t tempo, uint8_t timerFreq)
{
	if (timerFreq != 0 && tempo != 0)
	{
		return (double) 60000000 / (SEQ_PPQN * (125 * timerFreq)) * ((double) tempo / 256);
	}
	else
	{
		return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
	}
}


//  ************
//  RareSnesTrack
//  ************

RareSnesTrack::RareSnesTrack(RareSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
	bWriteGenericEventAsTextEvent = false;
}

bool RareSnesTrack::LoadTrackInit(uint32_t trackNum)
{
	if (!SeqTrack::LoadTrackInit(trackNum))
		return false;

	AddReverbNoItem(0);
	return true;
}

void RareSnesTrack::ResetVars(void)
{
	SeqTrack::ResetVars();

	cKeyCorrection = SEQ_KEYOFS;

	rptNestLevel = 0;
	spcNotePitch = 0;
	spcTranspose = 0;
	spcTransposeAbs = 0;
	spcInstr = 0;
	spcADSR = 0x8EE0;
	spcTuning = 0;
	defNoteDur = 0;
	useLongDur = false;
	altNoteByte1 = altNoteByte2 = 0x81;
}


double RareSnesTrack::GetTuningInSemitones(int8_t tuning)
{
	return 12.0 * log((1024 + tuning) / 1024.0) / log(2.0);
}

void RareSnesTrack::CalcVolPanFromVolLR(int8_t volLByte, int8_t volRByte, uint8_t& midiVol, uint8_t& midiPan)
{
	double volL = abs(volLByte) / 128.0;
	double volR = abs(volRByte) / 128.0;
	double vol;
	double pan;

	// linear conversion
	vol = (volL + volR) / 2;
	pan = volR / (volL + volR);

	// make it GM2 compatible
	ConvertPercentVolPanToStdMidiScale(vol, pan);

	midiVol = (uint8_t)(vol * 127 + 0.5);
	if (midiVol != 0)
	{
		midiPan = (uint8_t)(pan * 126 + 0.5);
		if (midiPan != 0)
		{
			midiPan++;
		}
	}
}

#define EVENT_WITH_MIDITEXT_START	bWriteGenericEventAsTextEventTmp = bWriteGenericEventAsTextEvent; bWriteGenericEventAsTextEvent = true;
#define EVENT_WITH_MIDITEXT_END	bWriteGenericEventAsTextEvent = bWriteGenericEventAsTextEventTmp;

bool RareSnesTrack::ReadEvent(void)
{
	RareSnesSeq* parentSeq = (RareSnesSeq*)this->parentSeq;
	uint32_t beginOffset = curOffset;
	if (curOffset >= 0x10000)
	{
		return false;
	}

	bool bWriteGenericEventAsTextEventTmp;
	uint8_t statusByte = GetByte(curOffset++);
	uint8_t newMidiVol, newMidiPan;
	bool bContinue = true;

	wstringstream desc;

	if (statusByte >= 0x80)
	{
		uint8_t noteByte = statusByte;

		// check for "reuse last key"
		if (noteByte == 0xe1)
		{
			noteByte = altNoteByte2;
		}
		else if (noteByte >= 0xe0)
		{
			noteByte = altNoteByte1;
		}

		uint8_t key = noteByte - 0x81;
		uint8_t spcKey = min(max(noteByte - 0x80 + 36 + spcTranspose, 0), 0x7f);

		uint16_t dur;
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
			// a note, add hints for instrument
			if (parentSeq->instrUnityKeyHints.find(spcInstr) == parentSeq->instrUnityKeyHints.end())
			{
				parentSeq->instrUnityKeyHints[spcInstr] = spcTransposeAbs;
				parentSeq->instrPitchHints[spcInstr] = (int16_t) round(GetTuningInSemitones(spcTuning) * 100.0);
			}
			if (parentSeq->instrADSRHints.find(spcInstr) == parentSeq->instrADSRHints.end())
			{
				parentSeq->instrADSRHints[spcInstr] = spcADSR;
			}

			spcNotePitch = RareSnesSeq::NOTE_PITCH_TABLE[spcKey];
			spcNotePitch = (spcNotePitch * (1024 + spcTuning) + (spcTuning < 0 ? 1023 : 0)) / 1024;

			//wostringstream ssTrace;
			//ssTrace << L"Note: " << key << L" " << dur << L" " << defNoteDur << L" " << (useLongDur ? L"L" : L"S") << L" P=" << spcNotePitch << std::endl;
			//OutputDebugString(ssTrace.str().c_str());

			uint8_t vel = 127;
			AddNoteByDur(beginOffset, curOffset-beginOffset, key, vel, dur);
			AddTime(dur);
		}
	}
	else
	{
		RareSnesSeqEventType eventType = (RareSnesSeqEventType)0;
		map<uint8_t, RareSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

		case EVENT_END:
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			bContinue = false;
			//loaded = true;
			break;

		case EVENT_PROGCHANGE:
		{
			uint8_t newProg = GetByte(curOffset++);
			spcInstr = newProg;
			AddProgramChange(beginOffset, curOffset-beginOffset, newProg, true);
			break;
		}

		case EVENT_PROGCHANGEVOL:
		{
			uint8_t newProg = GetByte(curOffset++);
			int8_t newVolL = (int8_t) GetByte(curOffset++);
			int8_t newVolR = (int8_t) GetByte(curOffset++);

			spcInstr = newProg;
			spcVolL = newVolL;
			spcVolR = newVolR;
			AddProgramChange(beginOffset, curOffset-beginOffset, newProg, true, L"Program Change, Volume");
			AddVolLRNoItem(spcVolL, spcVolR);
			break;
		}

		case EVENT_VOLLR:
		{
			int8_t newVolL = (int8_t) GetByte(curOffset++);
			int8_t newVolR = (int8_t) GetByte(curOffset++);

			spcVolL = newVolL;
			spcVolR = newVolR;
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Volume L/R");
			break;
		}

		case EVENT_VOLCENTER:
		{
			int8_t newVol = (int8_t) GetByte(curOffset++);

			spcVolL = newVol;
			spcVolR = newVol;
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Volume");
			break;
		}

		case EVENT_GOTO:
		{
			uint16_t dest = GetShort(curOffset); curOffset += 2;
			desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
			uint32_t length = curOffset - beginOffset;

			curOffset = dest;
			if (!IsOffsetUsed(dest) || rptNestLevel != 0) // nest level check is required for Stickerbrush Symphony
				AddGenericEvent(beginOffset, length, L"Jump", desc.str().c_str(), CLR_LOOPFOREVER);
			else
				AddLoopForever(beginOffset, length, L"Jump");
			break;
		}

		case EVENT_CALLNTIMES:
		{
			uint8_t times = GetByte(curOffset++);
			uint16_t dest = GetShort(curOffset); curOffset += 2;

			desc << L"Times: " << (int)times << L"  Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
			AddGenericEvent(beginOffset, curOffset-beginOffset, (times == 1 ? L"Pattern Play" : L"Pattern Repeat"), desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

			if (rptNestLevel == RARESNES_RPTNESTMAX)
			{
				pRoot->AddLogItem(new LogItem(L"Subroutine nest level overflow\n", LOG_LEVEL_ERR, L"RareSnesSeq"));
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
			uint16_t dest = GetShort(curOffset); curOffset += 2;

			desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)dest;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pattern Play", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

			if (rptNestLevel == RARESNES_RPTNESTMAX)
			{
				pRoot->AddLogItem(new LogItem(L"Subroutine nest level overflow\n", LOG_LEVEL_ERR, L"RareSnesSeq"));
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
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"End Pattern", desc.str().c_str(), CLR_TRACKEND, ICON_ENDREP);

			if (rptNestLevel == 0)
			{
				pRoot->AddLogItem(new LogItem(L"Subroutine nest level overflow\n", LOG_LEVEL_ERR, L"RareSnesSeq"));
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

			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Default Duration On", desc.str().c_str(), CLR_DURNOTE, ICON_NOTE);
			break;
		}

		case EVENT_DEFDUROFF:
			defNoteDur = 0;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Default Duration Off", desc.str().c_str(), CLR_DURNOTE, ICON_NOTE);
			break;

		case EVENT_PITCHSLIDEUP:
		{
			curOffset += 5;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide Up", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_PITCHSLIDEDOWN:
		{
			curOffset += 5;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide Down", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_PITCHSLIDEOFF:
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide Off", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_TEMPO:
		{
			uint8_t newTempo = GetByte(curOffset++);
			parentSeq->tempo = newTempo;
			AddTempoBPM(beginOffset, curOffset-beginOffset, parentSeq->GetTempoInBPM());
			break;
		}

		case EVENT_TEMPOADD:
		{
			int8_t deltaTempo = (int8_t) GetByte(curOffset++);
			parentSeq->tempo = (parentSeq->tempo + deltaTempo) & 0xff;
			AddTempoBPM(beginOffset, curOffset-beginOffset, parentSeq->GetTempoInBPM(), L"Tempo Add");
			break;
		}

		case EVENT_VIBRATOSHORT:
		{
			curOffset += 3;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato (Short)", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_VIBRATOOFF:
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato Off", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_VIBRATO:
		{
			curOffset += 4;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Vibrato", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_TREMOLOOFF:
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tremolo Off", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_TREMOLO:
		{
			curOffset += 4;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tremolo", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_ADSR:
		{
			uint16_t newADSR = GetShortBE(curOffset); curOffset += 2;
			spcADSR = newADSR;

			desc << L"ADSR: " << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)newADSR;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_MASTVOL:
		{
			uint8_t newVol = GetByte(curOffset++);
			AddMasterVol(beginOffset, curOffset-beginOffset, newVol & 0x7f);
			break;
		}

		case EVENT_MASTVOLLR:
		{
			int8_t newVolL = (int8_t) GetByte(curOffset++);
			int8_t newVolR = (int8_t) GetByte(curOffset++);
			int8_t newVol = min(abs(newVolL) + abs(newVolR), 255) / 2; // workaround: convert to mono
			AddMasterVol(beginOffset, curOffset-beginOffset, newVol, L"Master Volume L/R");
			break;
		}

		case EVENT_TUNING:
		{
			int8_t newTuning = (int8_t) GetByte(curOffset++);
			spcTuning = newTuning;
			desc << L"Tuning: " << (int)newTuning << L" (" << (int)(GetTuningInSemitones(newTuning) * 100 + 0.5) << L" cents)";
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tuning", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_TRANSPABS: // should be used for pitch correction of instrument
		{
			int8_t newTransp = (int8_t) GetByte(curOffset++);
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
			int8_t deltaTransp = (int8_t) GetByte(curOffset++);
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
			uint8_t newFeedback = GetByte(curOffset++);
			int8_t newVolL = (int8_t) GetByte(curOffset++);
			int8_t newVolR = (int8_t) GetByte(curOffset++);
			parentSeq->midiReverb = min(abs(newVolL) + abs(newVolR), 255) / 2;
			// TODO: update MIDI reverb value for each tracks?

			desc << L"Feedback: " << (int)newFeedback << L"  Volume: " << (int)newVolL << L", " << (int)newVolR;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo Param", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_ECHOON:
			EVENT_WITH_MIDITEXT_START
			AddReverb(beginOffset, curOffset-beginOffset, parentSeq->midiReverb, L"Echo On");
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_ECHOOFF:
			EVENT_WITH_MIDITEXT_START
			AddReverb(beginOffset, curOffset-beginOffset, 0, L"Echo Off");
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_ECHOFIR:
		{
			uint8_t newFIR[8];
			GetBytes(curOffset, 8, newFIR);
			curOffset += 8;

			desc << L"Filter: ";
			for (int iFIRIndex = 0; iFIRIndex < 8; iFIRIndex++)
			{
				if (iFIRIndex != 0)
					desc << L" ";
				desc << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)newFIR[iFIRIndex];
			}

			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo FIR", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_NOISECLK:
		{
			uint8_t newCLK = GetByte(curOffset++);
			desc << L"CLK: " << (int)newCLK;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Noise Frequency", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_NOISEON:
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Noise On", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_NOISEOFF:
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Noise Off", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_SETALTNOTE1:
			altNoteByte1 = GetByte(curOffset++);
			desc << L"Note: " << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)altNoteByte1;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Set Alt Note 1", desc.str().c_str(), CLR_CHANGESTATE, ICON_NOTE);
			break;

		case EVENT_SETALTNOTE2:
			altNoteByte2 = GetByte(curOffset++);
			desc << L"Note: " << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int)altNoteByte2;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Set Alt Note 2", desc.str().c_str(), CLR_CHANGESTATE, ICON_NOTE);
			break;

		case EVENT_PITCHSLIDEDOWNSHORT:
		{
			curOffset += 4;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide Down (Short)", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_PITCHSLIDEUPSHORT:
		{
			curOffset += 4;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Slide Up (Short)", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_LONGDURON:
			useLongDur = true;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Long Duration On", desc.str().c_str(), CLR_DURNOTE, ICON_NOTE);
			break;

		case EVENT_LONGDUROFF:
			useLongDur = false;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Long Duration Off", desc.str().c_str(), CLR_DURNOTE, ICON_NOTE);
			break;

		case EVENT_SETVOLADSRPRESET1:
		{
			int8_t newVolL = (int8_t) GetByte(curOffset++);
			int8_t newVolR = (int8_t) GetByte(curOffset++);
			uint16_t newADSR = GetShortBE(curOffset); curOffset += 2;

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
			int8_t newVolL = (int8_t) GetByte(curOffset++);
			int8_t newVolR = (int8_t) GetByte(curOffset++);
			uint16_t newADSR = GetShortBE(curOffset); curOffset += 2;

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
			int8_t newVolL = (int8_t) GetByte(curOffset++);
			int8_t newVolR = (int8_t) GetByte(curOffset++);
			uint16_t newADSR = GetShortBE(curOffset); curOffset += 2;

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
			int8_t newVolL = (int8_t) GetByte(curOffset++);
			int8_t newVolR = (int8_t) GetByte(curOffset++);
			uint16_t newADSR = GetShortBE(curOffset); curOffset += 2;

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
			int8_t newVolL = (int8_t) GetByte(curOffset++);
			int8_t newVolR = (int8_t) GetByte(curOffset++);
			uint16_t newADSR = GetShortBE(curOffset); curOffset += 2;

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
			EVENT_WITH_MIDITEXT_START
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Vol/ADSR Preset 1");
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_GETVOLADSRPRESET2:
			spcVolL = parentSeq->presetVolL[1];
			spcVolR = parentSeq->presetVolR[1];
			EVENT_WITH_MIDITEXT_START
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Vol/ADSR Preset 2");
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_GETVOLADSRPRESET3:
			spcVolL = parentSeq->presetVolL[2];
			spcVolR = parentSeq->presetVolR[2];
			EVENT_WITH_MIDITEXT_START
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Vol/ADSR Preset 3");
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_GETVOLADSRPRESET4:
			spcVolL = parentSeq->presetVolL[3];
			spcVolR = parentSeq->presetVolR[3];
			EVENT_WITH_MIDITEXT_START
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Vol/ADSR Preset 4");
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_GETVOLADSRPRESET5:
			spcVolL = parentSeq->presetVolL[4];
			spcVolR = parentSeq->presetVolR[4];
			EVENT_WITH_MIDITEXT_START
			AddVolLR(beginOffset, curOffset-beginOffset, spcVolL, spcVolR, L"Get Vol/ADSR Preset 5");
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_TIMERFREQ:
		{
			uint8_t newFreq = GetByte(curOffset++);
			parentSeq->timerFreq = newFreq;
			AddTempoBPM(beginOffset, curOffset-beginOffset, parentSeq->GetTempoInBPM(), L"Timer Frequency");
			break;
		}

		//case EVENT_CONDJUMP:
		//	break;

		//case EVENT_SETCONDJUMPPARAM:
		//	break;

		case EVENT_RESETADSR:
			spcADSR = 0x8FE0;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reset ADSR", L"ADSR: 8FE0", CLR_ADSR, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_RESETADSRSOFT:
			spcADSR = 0x8EE0;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reset ADSR (Soft)", L"ADSR: 8EE0", CLR_ADSR, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;

		case EVENT_VOICEPARAMSHORT:
		{
			uint8_t newProg = GetByte(curOffset++);
			int8_t newTransp = (int8_t) GetByte(curOffset++);
			int8_t newTuning = (int8_t) GetByte(curOffset++);

			desc << L"Program Number: " << (int)newProg << L"  Transpose: " << (int)newTransp << L"  Tuning: " << (int)newTuning << L" (" << (int)(GetTuningInSemitones(newTuning) * 100 + 0.5) << L" cents)";;

			// instrument
			spcInstr = newProg;
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
			uint8_t newProg = GetByte(curOffset++);
			int8_t newTransp = (int8_t) GetByte(curOffset++);
			int8_t newTuning = (int8_t) GetByte(curOffset++);
			int8_t newVolL = (int8_t) GetByte(curOffset++);
			int8_t newVolR = (int8_t) GetByte(curOffset++);
			uint16_t newADSR = GetShortBE(curOffset); curOffset += 2;

			desc << L"Program Number: " << (int)newProg << L"  Transpose: " << (int)newTransp << L"  Tuning: " << (int)newTuning << L" (" << (int)(GetTuningInSemitones(newTuning) * 100 + 0.5) << L" cents)";;
			desc << L"  Volume: " << (int)newVolL << L", " << (int)newVolR;
			desc << L"  ADSR: " << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int)newADSR;

			// instrument
			spcInstr = newProg;
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
			spcADSR = newADSR;

			break;
		}

		case EVENT_ECHODELAY:
		{
			uint8_t newEDL = GetByte(curOffset++);
			desc << L"Delay: " << (int)newEDL;
			EVENT_WITH_MIDITEXT_START
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo Delay", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
			EVENT_WITH_MIDITEXT_END
			break;
		}

		case EVENT_SETVOLPRESETS:
		{
			int8_t newVolL1 = (int8_t) GetByte(curOffset++);
			int8_t newVolR1 = (int8_t) GetByte(curOffset++);
			int8_t newVolL2 = (int8_t) GetByte(curOffset++);
			int8_t newVolR2 = (int8_t) GetByte(curOffset++);

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
			EVENT_WITH_MIDITEXT_START
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event", desc.str().c_str());
			EVENT_WITH_MIDITEXT_END
			pRoot->AddLogItem(new LogItem(wstring(L"Unknown Event - ") + desc.str(), LOG_LEVEL_ERR, wstring(L"RareSnesSeq")));
			bContinue = false;
			break;
		}
	}

	//wostringstream ssTrace;
	//ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
	//OutputDebugString(ssTrace.str().c_str());

	return bContinue;
}

void RareSnesTrack::OnTickBegin(void)
{
}

void RareSnesTrack::OnTickEnd(void)
{
}

void RareSnesTrack::AddVolLR(uint32_t offset, uint32_t length, int8_t spcVolL, int8_t spcVolR, const wchar_t* sEventName)
{
	uint8_t newMidiVol;
	uint8_t newMidiPan;
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

void RareSnesTrack::AddVolLRNoItem(int8_t spcVolL, int8_t spcVolR)
{
	uint8_t newMidiVol;
	uint8_t newMidiPan;
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
