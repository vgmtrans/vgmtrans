// The following sequence analysis code is based on the work of Sound Tester 774 from 2ch.net,
// author of so2mml. The code is based on his write-up of the format specifications.  Many thanks to him.

#include "stdafx.h"
#include "TriAcePS1Seq.h"
#include "TriAcePS1Format.h"
#include "ScaleConversion.h"
#include "SeqEvent.h"

DECLARE_FORMAT(TriAcePS1);

// ************
// TriAcePS1Seq
// ************

TriAcePS1Seq::TriAcePS1Seq(RawFile* file, ULONG offset)
: VGMSeq(TriAcePS1Format::name, file, offset)
{
	AddContainer<TriAcePS1ScorePattern>(aScorePatterns);
	UseLinearAmplitudeScale();
	UseReverb();
	AlwaysWriteInitialPitchBendRange(12, 0);
}

TriAcePS1Seq::~TriAcePS1Seq()
{
	DeleteVect<TriAcePS1ScorePattern>(aScorePatterns);
}


int TriAcePS1Seq::GetHeaderInfo(void)
{
	SetPPQN(0x30);

	header = AddHeader(dwOffset, 0xD5);
	header->AddSimpleItem(dwOffset+2, 2, L"Size");
	header->AddSimpleItem(dwOffset+0xB, 4, L"Song title");
	header->AddSimpleItem(dwOffset+0xF, 1, L"BPM");
	header->AddSimpleItem(dwOffset+0x10, 2, L"Time Signature");


	unLength = GetShort(dwOffset+2);
	tempoBPM = GetByte(dwOffset+0xF);
	bWriteInitialTempo = true;

	name = L"TriAce Seq";
	return true;
}

int TriAcePS1Seq::GetTrackPointers(void)
{
	VGMHeader* TrkInfoHeader = header->AddHeader(dwOffset+0x16, 6*32, L"Track Info Blocks");


	GetBytes(dwOffset+0x16, 6*32, &TrkInfos);
	for (int i=0; i<32; i++)
		if (TrkInfos[i].trkOffset != 0)
		{
			aTracks.push_back(new TriAcePS1Track(this, TrkInfos[i].trkOffset, 0));

			VGMHeader* TrkInfoBlock = TrkInfoHeader->AddHeader(dwOffset+0x16+6*i, 6, L"Track Info");
		}
	return true;
}

// **************
// TriAcePS1Track
// **************

TriAcePS1Track::TriAcePS1Track(TriAcePS1Seq* parentSeq, long offset, long length)
: SeqTrack(parentSeq, offset, length)
{
}

int TriAcePS1Track::LoadTrackMainLoop(U32 stopOffset, long stopDelta)
{
	TriAcePS1Seq* seq = (TriAcePS1Seq*)parentSeq;
	U32 scorePatternPtrOffset = dwOffset;
	U16 scorePatternOffset = GetShort(scorePatternPtrOffset);
	while (scorePatternOffset != 0xFFFF)
	{
		if (seq->patternMap[scorePatternOffset])
			seq->curScorePattern = NULL;
		else
		{
			TriAcePS1ScorePattern* pattern = new TriAcePS1ScorePattern(seq, scorePatternOffset);
			seq->patternMap[scorePatternOffset] = pattern;
			seq->curScorePattern = pattern;
			seq->aScorePatterns.push_back(pattern);
		}
		U32 endOffset = ReadScorePattern(scorePatternOffset);
		if (seq->curScorePattern)
			seq->curScorePattern->unLength = endOffset - seq->curScorePattern->dwOffset;
		AddSimpleItem(scorePatternPtrOffset, 2, L"Score Pattern Ptr");
		scorePatternPtrOffset += 2;
		scorePatternOffset = GetShort(scorePatternPtrOffset);
	}
	AddEndOfTrack(scorePatternPtrOffset, 2);
	unLength = scorePatternPtrOffset+2 - dwOffset;
	return true;
}


U32 TriAcePS1Track::ReadScorePattern(U32 offset)
{
	curOffset = offset;	//start at beginning of track
	impliedNoteDur = 0;	//reset the implied values (from event 0x9E)
	impliedVelocity = 0;
	while (ReadEvent())
		;
	return curOffset;
}

int TriAcePS1Track::ReadEvent(void)
{
	ULONG beginOffset = curOffset;

	BYTE status_byte = GetByte(curOffset++);
	BYTE event_dur = 0;

	//0-0x7F is a note event
	if (status_byte <= 0x7F)
	{
		event_dur = GetByte(curOffset++); //Delta time from "Note on" to "Next command(op-code)".
		BYTE note_dur;
		BYTE velocity;
		if (!impliedNoteDur) note_dur = GetByte(curOffset++);  //Delta time from "Note on" to "Note off".
		else note_dur = impliedNoteDur;
		if (!impliedVelocity) velocity = GetByte(curOffset++);
		else velocity = impliedVelocity;
		AddNoteByDur(beginOffset, curOffset-beginOffset, status_byte, velocity, note_dur);
	}
	else switch (status_byte)
	{
		case 0x80 :
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Score Pattern End", CLR_TRACKEND);
			return false;

		case 0x81 :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x82 :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x83 :			//program change
			{
				event_dur = GetByte(curOffset++);
				BYTE progNum = GetByte(curOffset++);
				BYTE bankNum = GetByte(curOffset++);

				//ATLTRACE("PROGRAM CHANGE   ProgNum: %X    BankNum: %X", progNum, bankNum);
				
				BYTE bank = (bankNum*2) + ((progNum > 0x7F) ? 1 : 0);
				if (progNum > 0x7F)
					progNum -= 0x80;

				//ATLTRACE("  SELECTING: prog %X bank %X\n", progNum, bank);

				AddBankSelectNoItem(bank);
				AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
			}
			break;

		case 0x84 :			//pitch bend
			{
				event_dur = GetByte(curOffset++);
				short bend = ((char)GetByte(curOffset++)) << 7;
				AddPitchBend(beginOffset, curOffset-beginOffset, bend);
			}
			break;

		case 0x85 :			//volume
			{
				event_dur = GetByte(curOffset++);
				BYTE val = GetByte(curOffset++);
				AddVol(beginOffset, curOffset-beginOffset, val);
			}
			break;

		case 0x86 :			//expression
			{
				event_dur = GetByte(curOffset++);
				BYTE val = GetByte(curOffset++);
				AddExpression(beginOffset, curOffset-beginOffset, val);
			}
			break;

		case 0x87 :			//pan
			{
				event_dur = GetByte(curOffset++);
				BYTE pan = GetByte(curOffset++);
				AddPan(beginOffset, curOffset-beginOffset, pan);
			}
			break;

		case 0x88 :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x89 :			//damper pedal
			{
				event_dur = GetByte(curOffset++);
				BYTE val = GetByte(curOffset++);
				AddSustainEvent(beginOffset, curOffset-beginOffset, (val>0));
			}
			break;

		case 0x8A :			//unknown (tempo?)
			{
				event_dur = GetByte(curOffset++);
				BYTE val = GetByte(curOffset++);
				AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event (tempo?)");
			}
			break;

		case 0x8D :			//Dal Segno: start point
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Dal Segno: start point", CLR_UNKNOWN);
			break;

		case 0x8E :			//Dal Segno: end point
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Dal Segno: end point", CLR_UNKNOWN);
			break;

		case 0x8F :			//rest
			{
				BYTE rest = GetByte(curOffset++);
				AddRest(beginOffset, curOffset-beginOffset, rest);
			}
			break;

		case 0x90 :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x92 :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x93 :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x94 :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset += 3;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x95 :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset, L"Unknown Event  (Tie?)");
			break;

		case 0x96 :			//Pitch Bend Range
			{
				event_dur = GetByte(curOffset++);
				BYTE semitones = GetByte(curOffset++);
				AddPitchBendRange(beginOffset, curOffset-beginOffset, semitones);
			}
			break;

		case 0x97 :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset += 4;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x99 :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x9A :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x9B :			//unknown
			event_dur = GetByte(curOffset++);
			curOffset += 5;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x9E :			//imply note params
			impliedNoteDur = GetByte(curOffset++);
			impliedVelocity = GetByte(curOffset++);
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Imply Note Params", CLR_CHANGESTATE);
			break;

		default :
			Alert(L"Unknown event opcode %X at %X", status_byte, beginOffset);
			AddUnknown(beginOffset, curOffset-beginOffset);
			return false;
	}

	if (event_dur)
		AddDelta(event_dur);

	return true;
}

// The following two functions are overridden so that events become children of the Score Patterns and not the tracks.
bool TriAcePS1Track::IsOffsetUsed(ULONG offset)
{
	return false;
}

void TriAcePS1Track::AddEvent(SeqEvent* pSeqEvent)
{
	TriAcePS1ScorePattern* pattern = ((TriAcePS1Seq*)parentSeq)->curScorePattern;
	if (pattern)
		pattern->AddItem(pSeqEvent);
}
