/**
 * GBA MusicPlayer2000 (Sappy)
 *
 * Special Thanks / Respects To:
 * GBAMusRiper (c) 2012 by Bregalad
 * http://www.romhacking.net/utilities/881/
 * http://www.romhacking.net/documents/%5B462%5Dsappy.txt
 */

#include "stdafx.h"
#include "MP2k.h"
#include "MP2kFormat.h"

DECLARE_FORMAT(MP2k);

using namespace std;

MP2kSeq::MP2kSeq(RawFile* file, uint32_t offset, std::wstring name)
: VGMSeq(MP2kFormat::name, file, offset, 0, name)
{
	bAllowDiscontinuousTrackData = true;
}

MP2kSeq::~MP2kSeq(void)
{
}

bool MP2kSeq::GetHeaderInfo(void)
{
	if (dwOffset + 2 > vgmfile->GetEndOffset())
	{
		return false;
	}

	nNumTracks = GetShort(dwOffset);
	if (nNumTracks == 0 || nNumTracks > 24)		//if there are no tracks or there are more tracks than allowed
	{
		return false;							//return an error, the sequence shall be deleted
	}
	if (dwOffset + 8 + nNumTracks * 4 > vgmfile->GetEndOffset())
	{
		return false;
	}

	VGMHeader* seqHdr = AddHeader(dwOffset, 8 + nNumTracks * 4, L"Sequence Header");
	seqHdr->AddSimpleItem(dwOffset, 1, L"Number of Tracks");
	seqHdr->AddSimpleItem(dwOffset + 1, 1, L"Unknown");
	seqHdr->AddSimpleItem(dwOffset + 2, 1, L"Priority");
	seqHdr->AddSimpleItem(dwOffset + 3, 1, L"Reverb");
	uint32_t dwInstPtr = GetWord(dwOffset + 4);
	seqHdr->AddPointer(dwOffset + 4, 4, dwInstPtr - 0x8000000, true, L"Instrument Pointer");
	for (unsigned int i = 0; i < nNumTracks; i++)
	{
		uint32_t dwTrackPtrOffset = dwOffset + 8 + i * 4;
		uint32_t dwTrackPtr = GetWord(dwTrackPtrOffset);
		seqHdr->AddPointer(dwTrackPtrOffset, 4, dwTrackPtr - 0x8000000, true, L"Track Pointer");
	}

	SetPPQN(0x30);
	return true;		//successful
}


bool MP2kSeq::GetTrackPointers(void)
{
	// Add each tracks
	for (unsigned int i = 0; i < nNumTracks; i++)
	{
		uint32_t dwTrackPtrOffset = dwOffset + 8 + i * 4;
		uint32_t dwTrackPtr = GetWord(dwTrackPtrOffset);
		aTracks.push_back(new MP2kTrack(this, dwTrackPtr - 0x8000000));
	}

	// Make seq offset the first track offset
	for (vector<SeqTrack*>::iterator itr = aTracks.begin(); itr != aTracks.end(); ++itr)
	{
		SeqTrack * track = (*itr);
		if (track->dwOffset < dwOffset)
		{
			if (unLength != 0)
			{
				unLength += (dwOffset - track->dwOffset);
			}
			dwOffset = track->dwOffset;
		}
	}

	return true;
}

//virtual bool Load(uint32_t offset)
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

/*void MP2kTrack::AddEvent(const char* sEventName, int nImage, unsigned long offset, unsigned long length, uint8_t color)
{
	if (mode == MODE_SCAN && bInLoop == false)
	{
		MP2kEvent* pMP2kEvent = new MP2kEvent(this, state);
		aEvents.push_back(pMP2kEvent);
		parentSeq->AddItem(pMP2kEvent, NULL, sEventName);
//		pMP2kEvent->AddToTreePlus(sEventName, nImage, pTreeItem, offset, length, color);
	}
}*/

bool MP2kTrack::ReadEvent(void)
{

	uint32_t beginOffset = curOffset;
	uint8_t status_byte = GetByte(curOffset++);
	bool bContinue = true;

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
			AddPitchBend(beginOffset, curOffset-beginOffset, ((int16_t)(status_byte-0x40))*128);
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
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Duration Note State + Note On (prev key and vel)", NULL, CLR_DURNOTE);
		}
		else
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Duration Note State", NULL, CLR_CHANGESTATE);
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
			{
				uint32_t destOffset = GetWord(curOffset) - 0x8000000;
				curOffset += 4;
				uint32_t length = curOffset - beginOffset;
				uint32_t dwEndTrackOffset = curOffset;

				curOffset = destOffset;
				if (!IsOffsetUsed(destOffset) || loopEndPositions.size() != 0)
				{
					AddGenericEvent(beginOffset, length, L"Goto", NULL, CLR_LOOPFOREVER);
				}
				else
				{
					bContinue = AddLoopForever(beginOffset, length, L"Goto");
				}

				// Add next end of track event
				if (readMode == READMODE_ADD_TO_UI)
				{
					if (dwEndTrackOffset < this->parentSeq->vgmfile->GetEndOffset())
					{
						uint8_t nextCmdByte = GetByte(dwEndTrackOffset);
						if (nextCmdByte == 0xB1)
						{
							AddEndOfTrack(dwEndTrackOffset, 1);
						}
					}
				}
				break;
			}
			break;
		case 0xB3 :		//Branch
			{
				uint32_t destOffset = GetWord(curOffset);
				curOffset+=4;
				loopEndPositions.push_back(curOffset);
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pattern Play", NULL, CLR_LOOP);
				curOffset = destOffset - 0x8000000;
			}
			break;
		case 0xB4 :		//Branch Break
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pattern End", NULL, CLR_LOOP);
			if (loopEndPositions.size() != 0)
			{
				curOffset = loopEndPositions.back();
				loopEndPositions.pop_back();
			}
			break;

		case 0xBA :
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Priority", NULL, CLR_PRIORITY);
			break;

		case 0xBB :
			{
				uint8_t tempo = GetByte(curOffset++)*2;		//tempo in bpm is data byte * 2
				AddTempoBPM(beginOffset, curOffset-beginOffset, tempo);
			}
			break;
		case 0xBC :
			transpose = GetByte(curOffset++);
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Key Shift", NULL, CLR_TRANSPOSE);
			break;

		case 0xBD :
			{
				uint8_t progNum = GetByte(curOffset++);
				AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
			}
			break;
		case 0xBE :
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Volume State", NULL, CLR_CHANGESTATE);
			state = STATE_VOL;
			break;
		case 0xBF :			//pan
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pan State", NULL, CLR_CHANGESTATE);
			state = STATE_PAN;
			break;
		case 0xC0 :				//pitch bend
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Bend State", NULL, CLR_CHANGESTATE);
			state = STATE_PITCHBEND;
			break;
		case 0xC1 :				//pitch bend range
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Bend Range", NULL, CLR_PITCHBENDRANGE);
			break;
		case 0xC2 :				//lfo speed
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Speed", NULL, CLR_LFO);
			break;
		case 0xC3 :				//lfo delay
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Delay", NULL, CLR_LFO);
			break;
		case 0xC4 :				//modulation depth
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Modulation Depth State", NULL, CLR_MODULATION);
			state = STATE_MODULATION;
			break;
		case 0xC5 :				//modulation type
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Modulation Type", NULL, CLR_MODULATION);
			break;

		case 0xC8 :
			{
				curOffset++;
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Microtune", NULL, CLR_PITCHBEND);
			}
			break;

		case 0xCD :				//extend command
			{
				// This command has a subcommand-byte and its arguments.
				// xIECV = 0x08;		//  imi.echo vol   ***lib
				// xIECL = 0x09;		//  imi.echo len   ***lib
				//
				// Probably, some games extends this command by their own code.

				uint8_t subCommand = GetByte(curOffset++);
				uint8_t subParam = GetByte(curOffset);

				if (subCommand == 0x08 && subParam <= 127)
				{
					curOffset++;
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo Volume", NULL, CLR_MISC);
				}
				else if (subCommand == 0x09 && subParam <= 127)
				{
					curOffset++;
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"Echo Length", NULL, CLR_MISC);
				}
				else
				{
					// Heuristic method
					while (subParam <= 127)
					{
						curOffset++;
						subParam = GetByte(curOffset);
					}
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"Extend Command", NULL, CLR_UNKNOWN);
				}
			}
			break;

		case 0xCE :
			{
				state = STATE_TIE_END;
				if (GetByte(curOffset) > 0x7F)			//yes, this seems to be how the actual driver code handles it.  Ex. Aria of Sorrow (U): 0x80D91C0 - handle 0xCE event
				{
					AddNoteOffNoItem(prevKey);
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"End Tie State + End Tie", NULL, CLR_TIE);
				}
				else
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"End Tie State", NULL, CLR_TIE);
			}
			break;

		case 0xCF :
			state = STATE_TIE;
			if (GetByte(curOffset) > 0x7F)
			{
				AddNoteOnNoItem(prevKey, prevVel);
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tie State + Tie (with prev key and vel)", NULL, CLR_TIE);
			}
			else
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tie State", NULL, CLR_TIE);
			break;

		default :
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;
		}
	}
	return bContinue;
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

MP2kEvent::MP2kEvent(MP2kTrack* pTrack, uint8_t stateType)
: SeqEvent(pTrack), eventState(stateType)
{
}
