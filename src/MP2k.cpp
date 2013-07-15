#include "stdafx.h"
#include "MP2k.h"
#include "MP2kFormat.h"

DECLARE_FORMAT(MP2k);

MP2kSeq::MP2kSeq(RawFile* file, ULONG offset)
: VGMSeq(MP2kFormat::name, file, offset)
{
}

MP2kSeq::~MP2kSeq(void)
{
}

int MP2kSeq::GetHeaderInfo(void)
{
	//unLength = GetShort(dwOffset+8);
	nNumTracks = GetShort(dwOffset);
	if (nNumTracks == 0 || nNumTracks > 24)		//if there are no tracks or there are more tracks than allowed
		return FALSE;							//return an error, the sequence shall be deleted
	SetPPQN(0x30);


	//name.Format("MP2000 Seq %d", GetCount());
	name = L"MP2000 Seq";

	return true;		//successful
}


int MP2kSeq::GetTrackPointers(void)
{
	for(int i=0; i<nNumTracks; i++)
		aTracks.push_back(new MP2kTrack(this, GetWord(dwOffset + 8 + i*4)-0x8000000));
	//	AddTrack(new MP2kTrack(this, GetWord(dwOffset + 8 + i*4)-0x8000000));
	//{
		//MP2kTrack* newTrack = new MP2kTrack(this);
		//newTrack->dwOffset = GetWord(dwOffset + 8 + i*4)-0x8000000;
		//aTracks.push_back(newTrack);
	//}	

	unLength = (dwOffset+8+nNumTracks*4) - aTracks.front()->dwOffset;
	dwOffset = aTracks.front()->dwOffset;			//make seq offset the first track offset

	return true;
}

//virtual int Load(UINT offset)
//{
//	return true;
//}

//  *********
//  MP2kTrack
//  *********


MP2kTrack::MP2kTrack(MP2kSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length), state(STATE_NOTE)
{
}

/*void MP2kTrack::AddEvent(const char* sEventName, int nImage, unsigned long offset, unsigned long length, BYTE color)
{
	if (mode == MODE_SCAN && bInLoop == false)
	{
		MP2kEvent* pMP2kEvent = new MP2kEvent(this, state);
		aEvents.push_back(pMP2kEvent);
		parentSeq->AddItem(pMP2kEvent, NULL, sEventName);
//		pMP2kEvent->AddToTreePlus(sEventName, nImage, pTreeItem, offset, length, color);
	}
}*/

int MP2kTrack::ReadEvent(void)
{

	ULONG beginOffset = curOffset;
	BYTE status_byte = GetByte(curOffset++);

	if (status_byte <= 0x7F)		//it's a status event (note, vel (fade), vol, more?)
	{
		switch (state)
		{
		case STATE_NOTE :
			if (GetByte(curOffset) <= 0x7F)	//if the next value is 0-127, it is a velocity value
			{
				current_vel = GetByte(curOffset++);
				if (GetByte(curOffset) <= 0x7F)	//if the next value is 0-127, it is an _unknown_ value
					curOffset++;
			}
			AddNoteByDur(beginOffset, curOffset-beginOffset, status_byte, current_vel, curDuration);
			break;
		case STATE_TIE :
			if (GetByte(curOffset) <= 0x7F)	//if the next value is 0-127, it is a velocity value
			{
				current_vel = GetByte(curOffset++);
				if (GetByte(curOffset) <= 0x7F)	//if the next value is 0-127, it is an _unknown_ value
					curOffset++;
			}
			AddNoteOn(beginOffset, curOffset-beginOffset, status_byte, current_vel, L"Tie");
			break;
		case STATE_TIE_END :
			AddNoteOff(beginOffset, curOffset-beginOffset, status_byte, L"End Tie");
			break;
		case STATE_VOL :
			AddVol(beginOffset, curOffset-beginOffset, status_byte);
			break;
		case STATE_PAN :
			AddPan(beginOffset, curOffset-beginOffset, status_byte);
			break;
		case STATE_PITCHBEND :
			AddPitchBend(beginOffset, curOffset-beginOffset, ((SHORT)(status_byte-0x40))*128);
			break;
		case STATE_MODULATION :
			AddModulation(beginOffset, curOffset-beginOffset, status_byte);
			break;
		}
	}
	else if (status_byte >= 0x80 && status_byte <= 0xB0)			//it's a rest event
		AddRest(beginOffset, curOffset-beginOffset, length_table[status_byte - 0x80]*2);
	else if (status_byte >= 0xD0)			//it's a Duration Note State event
	{
		state = STATE_NOTE;
		curDuration = length_table[status_byte - 0xCF]*2;
		if (GetByte(curOffset) > 0x7F)	//if the next value is greater than 0x7F, then we have an assumed note of prevKey and prevVel
		{
			AddNoteByDurNoItem(prevKey, prevVel, curDuration);
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Duration Note State + Note On (prev key and vel)", CLR_DURNOTE);
		}
		else
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Duration Note State", CLR_CHANGESTATE);
	}
	else if (status_byte >= 0xB1 && status_byte <= 0xCF)			//it's a special event
	{
		switch (status_byte)
		{
		case 0xB1 :
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			return false;
			break;
		case 0xB2 :
			curOffset+=4;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Goto", /*ICON_STARTREP,*/ CLR_LOOP);
			break;
		case 0xB3 :		//Branch
			{
				UINT destOffset = GetWord(curOffset);
				curOffset+=4;
				loopEndPos = curOffset;
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pattern Play", CLR_LOOP);
				curOffset = destOffset - 0x8000000;
				bInLoop = TRUE;
			}
			break;
		case 0xB4 :		//Branch Break
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pattern End", CLR_LOOP);
			if (bInLoop)
			{
				curOffset = loopEndPos;
				bInLoop = FALSE;
			}
			break;

		case 0xBA :
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Priority", CLR_PRIORITY);
			break;

		case 0xBB :
			{
				BYTE tempo = GetByte(curOffset++)*2;		//tempo in bpm is data byte * 2
				AddTempoBPM(beginOffset, curOffset-beginOffset, tempo);
			}
			break;
		case 0xBC :
			transpose = GetByte(curOffset++);
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Key Shift", CLR_TRANSPOSE);
			break;

		case 0xBD :
			{
				BYTE progNum = GetByte(curOffset++);
				if (progNum > 119)
					channel = 9;//SetChannel(9);	//possibly get rid of this
				AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
			}
			break;
		case 0xBE :
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Volume State", CLR_CHANGESTATE);
			state = STATE_VOL;
			break;
		case 0xBF :			//pan
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pan State", CLR_CHANGESTATE);
			state = STATE_PAN;
			break;
		case 0xC0 :				//pitch bend
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Bend State", CLR_CHANGESTATE);
			state = STATE_PITCHBEND;
			break;
		case 0xC1 :				//pitch bend range
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Bend Range", CLR_PITCHBENDRANGE);
			break;
		case 0xC2 :				//lfo speed
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Speed", CLR_LFO);
			break;
		case 0xC3 :				//lfo delay
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Delay", CLR_LFO);
			break;
		case 0xC4 :				//modulation depth
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Modulation Depth State", CLR_MODULATION);
			state = STATE_MODULATION;
			break;
		case 0xC5 :				//modulation type
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Modulation Type", CLR_MODULATION);
			break;

		case 0xC8 :
			{
				curOffset++;
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Microtune", CLR_PITCHBEND);
			}
			break;

		case 0xCD :				//extend command
			//curOffset++;		//don't know
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Extend Command", CLR_UNKNOWN);
			break;

		case 0xCE :
			{
				state = STATE_TIE_END;
				if (GetByte(curOffset) > 0x7F)			//yes, this seems to be how the actual driver code handles it.  Ex. Aria of Sorrow (U): 0x80D91C0 - handle 0xCE event
				{
					AddNoteOffNoItem(prevKey);
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"End Tie State + End Tie", CLR_TIE);
				}
				else
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"End Tie State", CLR_TIE);
			}
			break;

		case 0xCF :
			state = STATE_TIE;
			if (GetByte(curOffset) > 0x7F)
			{
				AddNoteOnNoItem(prevKey, prevVel);
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tie State + Tie (with prev key and vel)", CLR_TIE);
			}
			else
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tie State", CLR_TIE);
			break;

		default :
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"UNKNOWN", CLR_UNRECOGNIZED);
			break;
		}
	}
	return true;
}

/*void FFTSeq::OnSaveAsMidi(void)
{
	SaveAsMidi((LPTSTR)(name.GetString()), true);
}

void FFTSeq::OnSaveAllAsMidi(void)
{
	((BGM_WDPlugin*)pPlugin)->SaveAllBGMAsMidi();
}*/


//  *********
//  MP2kEvent
//  *********

MP2kEvent::MP2kEvent(MP2kTrack* pTrack, BYTE stateType)
: SeqEvent(pTrack), eventState(stateType)
{
}
