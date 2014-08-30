#ifdef _WIN32
	#include "stdafx.h"
#endif
#include "NDSSeq.h"
#include "NDSFormat.h"

DECLARE_FORMAT(NDS);

using namespace std;

NDSSeq::NDSSeq(RawFile* file, uint32_t offset, uint32_t length, wstring name)
: VGMSeq(NDSFormat::name, file, offset, length, name)
{
}

/*
NDSSeq::~NDSSeq(void)
{
}*/


bool NDSSeq::GetHeaderInfo(void)
{
	VGMHeader* SSEQHdr = AddHeader(dwOffset, 0x10, L"SSEQ Chunk Header");
	SSEQHdr->AddSig(dwOffset, 8);
	SSEQHdr->AddSimpleItem(dwOffset+8, 4, L"Size");
	SSEQHdr->AddSimpleItem(dwOffset+12, 2, L"Header Size");
	SSEQHdr->AddUnknownItem(dwOffset+14, 2);
	//SeqChunkHdr->AddSimpleItem(dwOffset, 4, L"Blah");
	unLength = GetShort(dwOffset+8);
	SetPPQN(0x30);
	return true;		//successful
}

bool NDSSeq::GetTrackPointers(void)
{
	VGMHeader* DATAHdr = AddHeader(dwOffset+0x10, 0xC, L"DATA Chunk Header");
	DATAHdr->AddSig(dwOffset+0x10, 4);
	DATAHdr->AddSimpleItem(dwOffset+0x10+4, 4, L"Size");
	DATAHdr->AddSimpleItem(dwOffset+0x10+8, 4, L"Data Pointer");
	uint32_t offset = dwOffset + 0x1C;
	uint8_t b = GetByte(offset);
	aTracks.push_back(new NDSTrack(this));

	if (b == 0xFE)		//FE XX XX signifies multiple tracks, each true bit in the XX values signifies there is a track for that channel
	{
		VGMHeader* TrkPtrs = AddHeader(offset, 0, L"Track Pointers");
		TrkPtrs->AddSimpleItem(offset, 3, L"Valid Tracks");
		offset += 3;	//but all we need to do is check for subsequent 0x93 track pointer events
		b = GetByte(offset);
		uint32_t songDelay = 0;
		while (b == 0x80)
		{
			register uint32_t value;
			register uint8_t c;
			uint32_t beginOffset = offset;
			offset++;
			if ( (value = GetByte(offset++)) & 0x80 )
			{
			   value &= 0x7F;
			   do
			   {
				 value = (value << 7) + ((c = GetByte(offset++)) & 0x7F);
			   } while (c & 0x80);
			}
			songDelay += value;
			TrkPtrs->AddSimpleItem(beginOffset, offset-beginOffset, L"Delay");
			//songDelay += SeqTrack::ReadVarLen(++offset);
			b = GetByte(offset);
			break;
		}
		while (b == 0x93)		//Track/Channel assignment and pointer.  Channel # is irrelevant
		{
			TrkPtrs->AddSimpleItem(offset, 5, L"Track Pointer");
			uint32_t trkOffset = GetByte(offset+2) + (GetByte(offset+3)<<8) + 
								(GetByte(offset+4)<<16) + dwOffset + 0x1C;
			NDSTrack* newTrack = new NDSTrack(this, trkOffset);
			aTracks.push_back(newTrack);
			//newTrack->
			offset += 5;
			b = GetByte(offset);
		}
		TrkPtrs->unLength = offset - TrkPtrs->dwOffset;
	}
	aTracks[0]->dwOffset = offset;
	aTracks[0]->dwStartOffset = offset;
	return true;
}

#if 0 /* old version */
bool NDSSeq::GetTrackPointers(void)
{
	uint32_t offset = dwOffset + 0x1F;
	uint8_t b = GetByte(offset);
	aTracks.push_back(new NDSTrack(this));
	while (b == 0x93)
	{
		uint32_t trkOffset = GetByte(offset+2) + (GetByte(offset+3)<<8) + 
							(GetByte(offset+4)<<16) + dwOffset + 0x1C;
		aTracks.push_back(new NDSTrack(this, trkOffset));
		offset += 5;
		b = GetByte(offset);
	}
	aTracks[0]->dwOffset = offset;
	return true;
}
#endif

//  ********
//  NDSTrack
//  ********

NDSTrack::NDSTrack(NDSSeq* parentFile, uint32_t offset, uint32_t length)
: SeqTrack(parentFile, offset, length)
{
	ResetVars();
	bDetermineTrackLengthEventByEvent = true;
}

void NDSTrack::ResetVars()
{
	jumpCount = 0;
	loopReturnOffset = 0;
	SeqTrack::ResetVars();
}
/*
void NDSTrack::SetChannelAndGroupFromTrkNum(int theTrackNum)
{
	if (theTrackNum > 8 && theTrackNum != 15)
		channel = theTrackNum+1;
	else if (theTrackNum == 15)
		channel = 9;
	else
		channel = theTrackNum;
	channelGroup = 0;
	pMidiTrack->SetChannelGroup(channelGroup);
}*/


bool NDSTrack::ReadEvent(void)
{
	uint32_t beginOffset = curOffset;
	uint8_t status_byte = GetByte(curOffset++);

	if (status_byte < 0x80) //then it's a note on event
	{
		uint8_t vel = GetByte(curOffset++);
		dur = ReadVarLen(curOffset);//GetByte(curOffset++);
		AddNoteByDur(beginOffset, curOffset-beginOffset, status_byte, vel, dur);
		if(noteWithDelta)
		{
			AddTime(dur);
		}
	}
	else switch (status_byte)
	{
	case 0x80:
		dur = ReadVarLen(curOffset);
		AddRest(beginOffset, curOffset-beginOffset, dur);
		break;

	case 0x81:
		{
			uint8_t newProg = (uint8_t) ReadVarLen(curOffset);
			AddProgramChange(beginOffset, curOffset-beginOffset, newProg);
		}
		break;

	case 0x93: // [loveemu] open track, however should not handle in this function
		curOffset += 4;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Open Track");
		break;

	case 0x94:
		{
			uint32_t jumpAddr = GetByte(curOffset) + (GetByte(curOffset+1)<<8)
							+ (GetByte(curOffset+2)<<16) + parentSeq->dwOffset + 0x1C;
			curOffset += 3;

			// Add an End Track if it exists afterward, for completeness sake
			if (readMode == READMODE_ADD_TO_UI && !IsOffsetUsed(curOffset))
			{
				if (GetByte(curOffset) == 0xFF)
					AddEndOfTrack(curOffset, 1);
			}

			curOffset = jumpAddr;
			return this->AddLoopForever(beginOffset, 4, L"Loop");
		}
		//curOffset += 4;
		//AddGenericEvent(beginOffset, curOffset-beginOffset, L"Jump (with count)", BG_CLR_RED);
		break;

	case 0x95:
		loopReturnOffset = curOffset + 3;
		AddGenericEvent(beginOffset, curOffset+3-beginOffset, L"Call", NULL, CLR_LOOP);
		curOffset = GetByte(curOffset) + (GetByte(curOffset+1)<<8)
				+ (GetByte(curOffset+2)<<16) + parentSeq->dwOffset + 0x1C;	
		break;

	case 0xA0: // [loveemu] (ex: Hanjuku Hero DS: NSE_45, New Mario Bros: BGM_AMB_CHIKA, Slime Morimori Dragon Quest 2: SE_187, SE_210, Advance Wars)
		{
			uint8_t subStatusByte;
			int16_t randMin;
			int16_t randMax;

			subStatusByte = GetByte(curOffset++);
			randMin = (signed) GetShort(curOffset);
			curOffset += 2;
			randMax = (signed) GetShort(curOffset);
			curOffset += 2;

			AddUnknown(beginOffset, curOffset-beginOffset, L"Cmd with Random Value");
		}
		break;

	case 0xA1: // [loveemu] (ex: New Mario Bros: BGM_AMB_SABAKU)
		{
			uint8_t subStatusByte = GetByte(curOffset++);
			uint8_t varNumber = GetByte(curOffset++);

			AddUnknown(beginOffset, curOffset-beginOffset, L"Cmd with Variable");
		}
		break;

	case 0xA2: // [loveemu] 
		{
			AddUnknown(beginOffset, curOffset-beginOffset, L"If");
		}
		break;

	case 0xB0: // [loveemu] (ex: Children of Mana: SEQ_BGM001)
	case 0xB1: // [loveemu] (ex: Advance Wars - Dual Strike: SE_TAGPT_COUNT01)
	case 0xB2: // [loveemu] 
	case 0xB3: // [loveemu] 
	case 0xB4: // [loveemu] 
	case 0xB5: // [loveemu] 
	case 0xB6: // [loveemu] (ex: Mario Kart DS: 76th sequence)
	case 0xB8: // [loveemu] (ex: Tottoko Hamutaro: MUS_ENDROOL, Nintendogs)
	case 0xB9: // [loveemu] 
	case 0xBA: // [loveemu] 
	case 0xBB: // [loveemu] 
	case 0xBC: // [loveemu] 
	case 0xBD: // [loveemu] 
		{
			uint8_t varNumber;
			int16_t val;
			const wchar_t* eventName[] = {
				L"Set Variable", L"Add Variable", L"Sub Variable", L"Mul Variable", L"Div Variable", 
				L"Shift Vabiable", L"Rand Variable", L"", L"If Variable ==", L"If Variable >=", 
				L"If Variable >", L"If Variable <=", L"If Variable <", L"If Variable !="
			};

			varNumber = GetByte(curOffset++);
			val = GetShort(curOffset);
			curOffset += 2;

			AddUnknown(beginOffset, curOffset-beginOffset, eventName[status_byte - 0xB0]);
		}
		break;

	case 0xC0:
		{
			uint8_t pan = GetByte(curOffset++);
			AddPan(beginOffset, curOffset-beginOffset, pan);
		}
		break;

	case 0xC1:
		vol = GetByte(curOffset++);
		AddVol(beginOffset, curOffset-beginOffset, vol);
		break;

	case 0xC2: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_BOSS1_)
		{
			uint8_t mvol = GetByte(curOffset++);
			AddUnknown(beginOffset, curOffset-beginOffset, L"Master Volume");
		}
		break;

	case 0xC3: // [loveemu] (ex: Puyo Pop Fever 2: BGM00)
		{
			int8_t transpose = (signed) GetByte(curOffset++);
			AddTranspose(beginOffset, curOffset-beginOffset, transpose);
//			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Transpose", NULL, BG_CLR_GREEN);
		}
		break;

	case 0xC4: // [loveemu] pitch bend (ex: Castlevania Dawn of Sorrow: BGM_BOSS2)
		{
			int16_t bend = (signed) GetByte(curOffset++) * 64;
			AddPitchBend(beginOffset, curOffset-beginOffset, bend);
		}
		break;

	case 0xC5: // [loveemu] pitch bend range (ex: Castlevania Dawn of Sorrow: BGM_BOSS2)
		{
			uint8_t semitones = GetByte(curOffset++);
			AddPitchBendRange(beginOffset, curOffset-beginOffset, semitones);
		}
		break;

	case 0xC6: // [loveemu] (ex: Children of Mana: SEQ_BGM000)
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Priority", NULL, CLR_CHANGESTATE);
		break;

	case 0xC7: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
		{
			uint8_t notewait = GetByte(curOffset++);
			noteWithDelta = (notewait != 0);
			AddUnknown(beginOffset, curOffset-beginOffset, L"Notewait Mode");
		}
		break;

	case 0xC8: // [loveemu] (ex: Hanjuku Hero DS: NSE_42)
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Tie");
		break;

	case 0xC9: // [loveemu] (ex: Hanjuku Hero DS: NSE_50)
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Portamento Control");
		break;

	case 0xCA: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
		{
			uint8_t amount = GetByte(curOffset++);
			AddModulation(beginOffset, curOffset-beginOffset, amount, L"Modulation Depth");
		}
		break;

	case 0xCB: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Modulation Speed");
		break;

	case 0xCC: // [loveemu] (ex: Children of Mana: SEQ_BGM001)
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Modulation Type");
		break;

	case 0xCD: // [loveemu] (ex: Phoenix Wright - Ace Attorney: BGM021)
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Modulation Range");
		break;

	case 0xCE: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
		{
			bool bPortOn = (GetByte(curOffset++) != 0);
			AddPortamento(beginOffset, curOffset-beginOffset, bPortOn);
		}
		break;

	case 0xCF: // [loveemu] (ex: Bomberman: SEQ_AREA04)
		{
			uint8_t portTime = GetByte(curOffset++);
			AddPortamentoTime(beginOffset, curOffset-beginOffset, portTime);
		}
		break;

	case 0xD0: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Attack Rate");
		break;

	case 0xD1: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Decay Rate");
		break;

	case 0xD2: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Sustain Level");
		break;

	case 0xD3: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Release Rate");
		break;

	case 0xD4: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Loop Start");
		break;

	case 0xD5:
		{
			uint8_t expression = GetByte(curOffset++);
			AddExpression(beginOffset, curOffset-beginOffset, expression);
		}
		break;

	case 0xD6: // [loveemu] 
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Print Variable");
		break;

	case 0xE0: // [loveemu] (ex: Children of Mana: SEQ_BGM001)
		curOffset += 2;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Modulation Delay");
		break;

	case 0xE1:
		{
			uint16_t bpm = GetShort(curOffset);
			curOffset += 2;
			AddTempoBPM(beginOffset, curOffset-beginOffset, bpm);
		}
		break;

	case 0xE3: // [loveemu] (ex: Hippatte! Puzzle Bobble: SEQ_1pbgm03)
		curOffset += 2;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Sweep Pitch");
		break;

	case 0xFC: // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
		AddUnknown(beginOffset, curOffset-beginOffset, L"Loop End");
		break;

	case 0xFD:
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Return", NULL, CLR_LOOP);
		curOffset = loopReturnOffset;
		break;

	case 0xFE: // [loveemu] allocate track, however should not handle in this function
		curOffset += 2;
		AddUnknown(beginOffset, curOffset-beginOffset, L"Allocate Track");
		break;

	case 0xFF:
		AddEndOfTrack(beginOffset, curOffset-beginOffset);
		return false;

	default:
		AddUnknown(beginOffset, curOffset-beginOffset);
		return false;
	}
	return true;
}
