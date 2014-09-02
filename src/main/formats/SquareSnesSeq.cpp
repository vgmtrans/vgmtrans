#include "pch.h"
#include "SquSnesSeq.h"
#include "SquSnesFormat.h"

const uint8_t durtbl[14] = { 0xBF, 0x8F, 0x5F, 0x47, 0x2F, 0x23, 0x1F, 0x17, 0x0F, 0x0B, 0x07, 0x05, 0x02, 0x00 };


DECLARE_FORMAT(SquSnes);

//  **********
//  SquSnesSeq
//  **********


SquSnesSeq::SquSnesSeq(RawFile* file, uint32_t seqdataOffset, uint32_t instrtableOffset)
: VGMSeq(SquSnesFormat::name, file, seqdataOffset), instrtable_offset(instrtableOffset)
{
}

SquSnesSeq::~SquSnesSeq(void)
{
}

bool SquSnesSeq::GetHeaderInfo(void)
{
	SetPPQN(0x60);
	name = L"Square SNES Seq";

	for (int i = 0; i < 8; i++)
	{
		uint16_t trkOffset = GetShort(dwOffset + i*2);
		aTracks.push_back(new SquSnesTrack(this, trkOffset));
	}

//	unLength = 0x7A4;

	/*int i = 0x10;
	uint8_t testByte = GetByte(dwOffset + i)
	while (testByte != 0xFF)
	{
		//TODO: add perc table code
		i += 5;
		testByte = GetByte(dwOffset + i);
	}*/

	return true;		//successful
}


bool SquSnesSeq::GetTrackPointers(void)
{
/*	for (int i=0; i<8; i++)
	{
		uint16_t trkOff = GetShort(dwOffset+i*2);
		if (trkOff)
			aTracks.push_back(new SquSnesTrack(this, trkOff);
	}
*/	return true;
}


//  ************
//  SquSnesTrack
//  ************

SquSnesTrack::SquSnesTrack(SquSnesSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
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


bool SquSnesTrack::ReadEvent(void)
{
	uint32_t beginOffset = curOffset;
	uint8_t status_byte = GetByte(curOffset++);

	if (status_byte < 0xC4) //then it's a note
	{
		uint8_t dur = status_byte / 14;
		uint8_t note = status_byte % 14;

		if (dur == 13)
			dur = GetByte(curOffset++);
		else
			dur = durtbl[dur];
		dur = (dur+1) << 1;

		if (note < 12)
		{
			uint8_t notedur = dur;
			uint8_t realNote = 12 * (octave) + note;		//NEEDS MODIFYING
			//if (percmode)
			//{

			//}

			//if (perckey)
			//{

			//}

			//some portamento crap goes here
			AddNoteByDur(beginOffset, curOffset-beginOffset, realNote, vel, notedur);
		}
		else if (note == 13)			//tie
		{
			MakePrevDurNoteEnd();
			AddUnknown(beginOffset, curOffset-beginOffset, L"Tie");
		}
		else							//rest
		{
			AddUnknown(beginOffset, curOffset-beginOffset, L"Rest");
		}

		AddTime(dur);
	}
	else								//command
	{
		switch (status_byte)
		{
		case 0xD1:							//tempo
			{
				uint8_t tempo = GetByte(curOffset++);
				AddTempo(beginOffset, curOffset-beginOffset, tempo * 125 * 48);
			}
			break;

		case 0x00:							//tempo fade
			
			break;

		case 0xE0:							//volume
		case 0xE2:							//ditto
			{
				uint8_t vol = GetByte(curOffset++);
				AddVol(beginOffset, curOffset-beginOffset, vol);
			}
			break;

		case 0xE4:							//volume fade
			break;

		case 0xE7:							//balance/pan
			{
				uint8_t pan = GetByte(curOffset++);
				AddPan(beginOffset, curOffset-beginOffset, pan >> 1);
			}
			break;

		case 0xE8:							//balance/pan fade
			break;

		case 0xDE:							//program change
			{
				uint8_t newProg = GetByte(curOffset++);
				AddProgramChange(beginOffset, curOffset-beginOffset, newProg);
			}
			break;

		case 0xC6:							//set octave
			{
				uint8_t newOctave = GetByte(curOffset++);
				AddSetOctave(beginOffset, curOffset-beginOffset, newOctave);
			}
			break;

		case 0xC4:							//increment octave
		case 0xF6:
		case 0xFE:
		case 0xFF:
			AddIncrementOctave(beginOffset, curOffset-beginOffset);
			break;

		case 0xC5:
			AddDecrementOctave(beginOffset, curOffset-beginOffset);
			break;

		case 0xF8:							//begin slur
			break;

		case 0xF9:							//end slur
			break;

		case 0xEE:							//percmode on
			break;

		case 0xEF:							//percmode off
			break;

		case 0xE5:							//portamento
			break;

		case 0xD2:							//begin repeat
		case 0xD3:
		case 0xD4:
			rptcnt[++rpt] = GetByte(curOffset++);
			rptpos[rpt] = curOffset;
			rptcur[rpt] = 0;
			//curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset, L"Begin Repeat");
			break;

		case 0xD5:							//end repeat
			AddUnknown(beginOffset, curOffset-beginOffset, L"End Repeat");
			if (--rptcnt[rpt] <= 0)
				rpt--;
			else
			{
				curOffset = rptpos[rpt];
				octave = rptoct[rpt];
			}			
			break;

		case 0xD7:							//mark loop point
			AddUnknown(beginOffset, curOffset-beginOffset, L"Mark Return Point");
			break;

		case 0xD0:							//goto
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			return false;

		case 0xFA:							//echo on
			AddUnknown(beginOffset, curOffset-beginOffset, L"Echo On");
			break;

		case 0xFB:							//echo on
			AddUnknown(beginOffset, curOffset-beginOffset, L"Echo Off");
			break;
		}
	}

	return true;
}
