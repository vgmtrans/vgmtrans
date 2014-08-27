#ifdef _WIN32
	#include "stdafx.h"
#endif
#include "SquarePS2.h"
#include "SquarePS2Format.h"

DECLARE_FORMAT(SquarePS2)

// ******
// BGMSeq
// ******

BGMSeq::BGMSeq(RawFile* file, uint32_t offset)
: VGMSeq(SquarePS2Format::name, file, offset)
{
	UseReverb();
}

BGMSeq::~BGMSeq(void)
{
}

bool BGMSeq::GetHeaderInfo(void)
{
	nNumTracks = GetByte(dwOffset+8);
	seqID = GetShort(dwOffset+4);
	assocWDID = GetShort(dwOffset+6);
	SetPPQN(GetShort(dwOffset+0xE));
	name = L"BGM Seq";
	return true;
}

bool BGMSeq::GetTrackPointers(void)
{
	uint32_t pos = dwOffset+0x20;    //start at first track (fixed offset)
	for(int i=0; i<nNumTracks; i++)
	{
		uint32_t trackSize = GetWord(pos);		//get the track size (first word before track data)
		aTracks.push_back(new BGMTrack(this, pos+4, trackSize));
		pos += trackSize+4;				//jump to the next track
	}
	return true;
}

// ********
// BGMTrack
// ********


BGMTrack::BGMTrack(BGMSeq* parentSeq, long offset, long length)
: SeqTrack(parentSeq, offset, length)
{
}


bool BGMTrack::ReadEvent(void)
{
	int value1;

	uint32_t beginOffset = curOffset;
	AddDelta(ReadVarLen(curOffset));

	uint8_t status_uint8_t = GetByte(curOffset++);

	switch (status_byte)
	{
	case 0x00 :			//end of track
		AddEndOfTrack(beginOffset, curOffset-beginOffset);
		return false;
		break;

	case 0x02 :			// Loop begin
//		loop_start = j;
		//rest_time += current_delta_time;
		//if (nScanMode == MODE_SCAN)
		//	AddBGMEvent("Loop Begin", ICON_STARTREP, aBGMTracks[cur_track]->pTreeItem, offsetAtDelta, j-offsetAtDelta, BG_CLR_CYAN);
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop Begin", BG_CLR_CYAN);
		break;

	case 0x03 :			// Loop end
//		loop_end = j;
//		loop_counter++;
		//if (loop_counter < num_loops)
		//	j = loop_start;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop End", BG_CLR_CYAN);
		break;

	case 0x04 :			//end of track?
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0x08 :			//set tempo
		{
			uint8_t bpm = GetByte(curOffset++);
			AddTempoBPM(beginOffset, curOffset-beginOffset, bpm);
		}
		break;
		
	case 0x0A :	
	case 0x0D :		//no idea... found at beginning of track
	case 0x28 :		//no idea.  ffxi 128.bgm
	case 0x31 :		//in FFXII prologue
	case 0x34 :
	case 0x35 :		//no clue, found in 108.bgm
	case 0x3E :		//no clue, found in 104.bgm of kingdomhearts - traverse town
	case 0x58 :
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0x0C :			//time signature?
		{
			uint8_t numer = GetByte(curOffset++);
			uint8_t denom = GetByte(curOffset++);
			AddTimeSig(beginOffset, curOffset-beginOffset, numer, denom, parentSeq->GetPPQN());

			//for (value3 = 0; ((value2&1) != TRUE) && (value3 < 8); ++value3)	//while 
			//	value2 >>= 1;
			//RecordMidiTimeSig(current_delta_time, value1, value3, usiPPQN/4, 8, hFile); 
		}
		break;

	case 0x10 :			//play previous key, no velocity param
		AddNoteOn(beginOffset, curOffset-beginOffset, prevKey, prevVel);
		break;

	case 0x11 :							//key on with velocity
		key = GetByte(curOffset++);
		vel = GetByte(curOffset++);
		AddNoteOn(beginOffset, curOffset-beginOffset, key, vel);
		break;
			
	case 0x12 :							//key on with prev velocity
		key = GetByte(curOffset++);
		AddNoteOn(beginOffset, curOffset-beginOffset, key, prevVel);
		break;

	case 0x13 :					// play previous key with velocity param
		vel = GetByte(curOffset++);
		AddNoteOn(beginOffset, curOffset-beginOffset, prevKey, vel);
		break;

	case 0x18 :
		AddNoteOff(beginOffset, curOffset-beginOffset, prevKey, L"Note off (prev key)");
		prevKey = key;
		break;

	case 0x19 :		//don't know
		curOffset += 2;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0x1A :
		key = GetByte(curOffset++);
		AddNoteOff(beginOffset, curOffset-beginOffset, key);
		prevKey = key;
		break;

	case 0x20 :		//assign instrument
		{
			uint8_t progNum = GetByte(curOffset++);
			AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
		}
		break;

	case 0x22 :		//set volume
		vol = GetByte(curOffset++);			//expression value
		AddVol(beginOffset, curOffset-beginOffset, vol);
		break;

	case 0x24 :		//expression
		{
			uint8_t expression = GetByte(curOffset++);			//expression value
			AddExpression(beginOffset, curOffset-beginOffset, expression);
		}
		break;

	case 0x26 :		//pan?
		{
			uint8_t pan = GetByte(curOffset++);
			AddPan(beginOffset, curOffset-beginOffset, pan);
		}
		break;

	case 0x3C :		//Sustain Pedal
		value1 = GetByte(curOffset++);
		AddSustainEvent(beginOffset, curOffset-beginOffset, (value1>0));
		break;

	case 0x40 :		//found in ffxi 208.bgm couple times
	case 0x48 :		//found in ffx-2 34.bgm - Yuna's Theme
	case 0x50 :		//found in bgm 048 of FFX-2, that's ost song "101 Eternity ~Memory of Lightwaves~.psf2"
		curOffset += 3;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0x47 :		//found in ffxi 208.bgm couple times
		curOffset += 2;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0x5C :		//pitch bend		I SHOULD GO BACK AND VERIFY THE RANGE OF THE PITCH BEND
		//curOffset+=2;
		{
			uint8_t lsb = GetByte(curOffset++);
			uint8_t msb = GetByte(curOffset++);
			AddPitchBendMidiFormat(beginOffset, curOffset-beginOffset, lsb, msb);
		}

		//value1 = assocDoc->GetByte(j++);
		//value2 = assocDoc->GetByte(j++);
		//RecordMidiPitchBend(current_delta_time, value1, value2, hFile);
		break;

	case 0x5D :		//Portamento?  found in ffxi 128.bgm
		curOffset++;
		//value1 = assocDoc->GetByte(j++);
		//RecordMidiControllerEvent(65, current_delta_time, 0x7F, hFile);	//portamento ON
		//RecordMidiControllerEvent(5, 0, value1, hFile);					//portamento time
		break;

	case 0x60 :		//no clue, part of init?
	case 0x61 :		//no clue, part of init?
	case 0x7F :		//no clue, part of init?
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	default :
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"UNKNOWN", BG_CLR_RED);
		break;

	}
	return true;
}