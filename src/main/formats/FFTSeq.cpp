#include "stdafx.h"
#include "FFTSeq.h"
#include "FFTFormat.h"

DECLARE_FORMAT(FFT);

using namespace std;

//  ******
//  FFTSeq
//  ******


FFTSeq::FFTSeq(RawFile* file, uint32_t offset)
: VGMSeq(FFTFormat::name, file, offset)
{
	AlwaysWriteInitialVol(127);
	UseLinearAmplitudeScale();
	UseReverb();
}

FFTSeq::~FFTSeq(void)
{
}

bool FFTSeq::GetHeaderInfo(void)
{

//-----------------------------------------------------------
//	Written by "Sound tester 774" in "内蔵音源をMIDI変換するスレ(in http://www.2ch.net)"
//	2009. 6.17 (Wed.)
//	2009. 6.30 (Thu.)
//-----------------------------------------------------------
					unLength		= GetShort(dwOffset+0x08);
					nNumTracks		= GetByte(dwOffset+0x14);	//uint8_t (8bit)		GetWord() から修正
	unsigned char	cNumPercussion	= GetByte(dwOffset+0x15);	//uint8_t (8bit)	Quantity of Percussion struct
//	unsigned char	cBankNum		= GetByte(dwOffset+0x16);
					assocWdsID		= GetShort(dwOffset+0x16);	//uint16_t (16bit)	Default program bank No.
	unsigned short	ptSongTitle		= GetShort(dwOffset+0x1E);	//uint16_t (16bit)	Pointer of music title (AscII strings)
	unsigned short	ptPercussionTbl	= GetShort(dwOffset+0x20);	//uint16_t (16bit)	Pointer of Percussion struct

	int titleLength = ptPercussionTbl-ptSongTitle;
	char* songtitle = new char[titleLength];
	GetBytes(dwOffset+ptSongTitle, titleLength, songtitle);
	this->name = string2wstring(string(songtitle));
	delete[] songtitle;

	//wostringstream songnameStream;
	//songnameStream << songtitle;
	//this->name = songnameStream.str(); 

//	name = L"smds Seq";

	VGMHeader* hdr = AddHeader(dwOffset, 0x22);					//ヘッダー情報も、16進数画面に出力する。
																//Header information set

	hdr->AddSig(dwOffset, 4);
	hdr->AddSimpleItem(dwOffset+0x08, 2, L"Size");
	hdr->AddSimpleItem(dwOffset+0x14, 1, L"Track count");
	hdr->AddSimpleItem(dwOffset+0x15, 1, L"Drum instrument count");
	hdr->AddSimpleItem(dwOffset+0x16, 1, L"Associated Sample Set ID");
	hdr->AddSimpleItem(dwOffset+0x1E, 2, L"Song title Pointer");
	hdr->AddSimpleItem(dwOffset+0x20, 2, L"Drumkit Data Pointer");

	VGMHeader* trackPtrs = AddHeader(dwOffset+0x22, nNumTracks*2, L"Track Pointers");
	for (unsigned int i = 0; i < nNumTracks; i++)
		trackPtrs->AddSimpleItem(dwOffset+0x22+i*2, 2, L"Track Pointer");
	VGMHeader* titleHdr = AddHeader(dwOffset+ptSongTitle, titleLength, L"Song Name");

//	if(cNumPercussion!=0){										//これ、やっぱ、いらない。
//		hdr->AddSimpleItem(dwOffset+ptPercussionTbl, cNumPercussion*5, L"Drumkit Struct");
//	}
//-----------------------------------------------------------

	SetPPQN(0x30);

	int i = 0;
	uint8_t j = 1;


//	while (j && j != '.')
//	{
////		j = GetByte(dwOffset+0x22+nNumTracks*2+2 + i++);		//修正
//		j = GetByte(dwOffset + ptSongTitle + (i++));
//		name += (char)j;
//	}


//	hdr->AddSimpleItem(dwOffset + ptMusicTile, i, L"Music title");		//やっぱ書かないでいいや。


	return true;		//successful
}


bool FFTSeq::GetTrackPointers(void)
{
	for(unsigned int i=0; i<nNumTracks; i++)
		aTracks.push_back(new FFTTrack(this, GetShort(dwOffset+0x22+(i*2)) + dwOffset));
	return true;
}


//  ********
//  FFTTrack
//  ********

FFTTrack::FFTTrack(FFTSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length)
{
	ResetVars();
}

void FFTTrack::ResetVars()
{
	bNoteOn = false;
	infiniteLoopPt = 0;
	loop_layer = 0;
	
	memset(loopID, 0, sizeof(loopID));
	memset(loop_counter, 0, sizeof(loop_counter));
	memset(loop_repeats, 0, sizeof(loop_repeats));
	memset(loop_begin_loc, 0, sizeof(loop_begin_loc));
	memset(loop_end_loc, 0, sizeof(loop_end_loc));
	memset(loop_octave, 0, sizeof(loop_octave));
	memset(loop_end_octave, 0, sizeof(loop_end_octave));
	SeqTrack::ResetVars();

	octave = 3;
}


//--------------------------------------------------
//Revisions:
//	2009. 6.17(Wed.) :	Re-make by "Sound tester 774" in "内蔵音源をMIDI変換するスレ(in http://www.2ch.net)"
//						Add un-known command(op-code).
//--------------------------------------------------
bool FFTTrack::ReadEvent(void)
{
	uint32_t beginOffset = curOffset;
	uint8_t status_byte = GetByte(curOffset++);

	if (status_byte < 0x80) //then it's a note on event
	{
		uint8_t			data_byte		= GetByte(curOffset++);
		uint8_t			vel				= status_byte;							//Velocity
		uint8_t			relative_key	= (data_byte / 19);						//Note
		unsigned int	iDeltaTime		= delta_time_table[data_byte % 19];		//Delta Time

		if(iDeltaTime==0)	
			iDeltaTime	= GetByte(curOffset++);		//Delta time
		if(bNoteOn)	
			AddNoteOffNoItem(prevKey);
		AddNoteOn(beginOffset, curOffset-beginOffset, octave*12 + relative_key, vel);
		AddTime(iDeltaTime);

//		if (delta_time_table[delta_byte] == 0)
//			AddNoteOn(beginOffset, curOffset-beginOffset+1, octave*12 + relative_key, vel);
//		} else {
//			AddNoteOn(beginOffset, curOffset-beginOffset, octave*12 + relative_key, vel);
//		}

		bNoteOn = true;

//		AddDelta(delta_time_table[delta_byte]);
//		if (delta_time_table[delta_byte] == 0)
//			AddDelta(GetByte(curOffset++));

	}
	else switch (status_byte)
	{

	//--------
	//Rest & Tie
	case 0x80 :			//rest + noteOff
		{
			uint8_t restTicks = GetByte(curOffset++);
			if (bNoteOn)
				AddNoteOffNoItem(prevKey);
			AddRest(beginOffset, curOffset-beginOffset, restTicks);
			bNoteOn = false;
		}
		break;

	case 0x81 :			//hold (Tie)
		AddTime(GetByte(curOffset++));
		AddHold(beginOffset, curOffset-beginOffset, L"Tie");
		break;

	//--------
	//Track Event
	case 0x90 :			//end of track
		if (infiniteLoopPt == 0)
		{
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			return false;
		}
		octave = infiniteLoopOctave;
		curOffset = infiniteLoopPt;
		return AddLoopForever(beginOffset, 1);

	case 0x91 :			//Dal Segno. (Infinite loop)
		infiniteLoopPt = curOffset;
		infiniteLoopOctave = octave;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Track Repeat Point", NULL, CLR_LOOP);
		//AddEventItem(_T("Repeat"), ICON_STARTREP, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);
		break;

	//--------
	//Octave Event
	case 0x94 :			//Set octave
		AddSetOctave(curOffset-2, 2, GetByte(curOffset++));
		break;

	case 0x95 :			//Inc octave
		AddIncrementOctave(beginOffset, curOffset-beginOffset);
		break;

	case 0x96 :			//Dec octave
		AddDecrementOctave(beginOffset, curOffset-beginOffset);
		break;

	//--------
	//Time signature
	case 0x97 :			//Time signature
		{
			uint8_t numer = GetByte(curOffset++);
			uint8_t denom = GetByte(curOffset++);
			AddTimeSig(beginOffset, curOffset-beginOffset, numer, denom, (uint8_t)parentSeq->GetPPQN());
		}
		break;

	//--------
	//Repeat Event
	case 0x98:			//Repeat Begin
		{
			bInLoop = false;
			uint8_t loopCount = GetByte(curOffset++);
			loop_layer++;
			loop_begin_loc[loop_layer] = curOffset;
			loop_counter[loop_layer] = 0;
			loop_repeats[loop_layer] = loopCount-1;
			loop_octave[loop_layer] = octave;			//1,Sep.2009 revise
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Repeat Begin", NULL, CLR_LOOP);
			//AddEventItem("Loop Begin", ICON_STARTREP, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);
		}
		break;

	case 0x99:			//Repeat End
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Repeat End", NULL, CLR_LOOP);
		//AddEventItem("Loop End", ICON_ENDREP, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);
		if (loopID[loop_layer] != curOffset) // hitting loop end for first time
		{
			bInLoop = true;
			loopID[loop_layer] = curOffset;
			loop_end_loc[loop_layer] = curOffset;
			curOffset = loop_begin_loc[loop_layer];
			loop_end_octave[loop_layer] = octave;		//1,Sep.2009 revise
			octave = loop_octave[loop_layer];			//1,Sep.2009 revise
			loop_counter[loop_layer]++;
		}
		else if (loop_counter[loop_layer] < loop_repeats[loop_layer]) //need to repeat loop, not first time
		{
			loop_counter[loop_layer]++;
			curOffset = loop_begin_loc[loop_layer];
			octave = loop_octave[loop_layer];			//1,Sep.2009 revise
		}
		else		//loop counter exhausted, end of loop
		{
			loopID[loop_layer]=0;
			loop_layer--;
			if (loop_counter[loop_layer] == 0)
				bInLoop = false;
		}
		break;

	case 0x9A:			//Repeat break on last repeat
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Repeat Break", NULL, CLR_LOOP);
		//AddEventItem("Loop Break", ICON_ENDREP, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);
		if (loop_counter[loop_layer] == loop_repeats[loop_layer])	// if this is the last run of the loop, we break
		{
			curOffset = loop_end_loc[loop_layer];  //jump to loop end point
			octave = loop_end_octave[loop_layer];  //set base_key to loop end base key
			loopID[loop_layer]=0;
			loop_layer--;
			if (loop_counter[loop_layer] == 0)
				bInLoop = false;
		}
		break;

	//--------
	//General Event
	case 0xA0 :			//set tempo
		{
			uint8_t cTempo = (GetByte(curOffset++) * 256) / 218;
			AddTempoBPM(beginOffset, curOffset-beginOffset, cTempo);
		}
		break;

	case 0xA2 :			//tempo slide
		{
			uint8_t cTempoSlideTimes	=  GetByte(curOffset++);		//slide times [ticks]
			uint8_t cTempoSlideTarget	= (GetByte(curOffset++) * 256) / 218;		//Target Panpot
//			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tempo slide", BG_CLR_WHEAT);
			AddTempoBPM(beginOffset, curOffset-beginOffset, cTempoSlideTarget, L"Tempo slide");
		}
		break;

	case 0xA9 :			//unknown
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xAC :			//program change
		{
			uint8_t progNum = GetByte(curOffset++);
			// to do:	Bank select(op-code:0xFE)
			//			Default bank number is "assocWdsID".
			AddBankSelectNoItem(progNum/128);
			if (progNum > 127)
				progNum %= 128;
				// When indicate 255 in progNum, program No.0 & bank No.254 is selected.
			AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
		}
		break;

	case 0xAD :			//unknown
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xAE :			//Percussion On
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Percussion On", NULL, CLR_CHANGESTATE);
		break;

	case 0xAF :			//Percussion Off
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Percussion Off", NULL, CLR_CHANGESTATE);
		break;

	case 0xB0 :			//Slur On
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Slur On", NULL, CLR_PORTAMENTO);
		break;

	case 0xB1 :			//Slur Off
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Slur Off", NULL, CLR_PORTAMENTO);
		break;

	case 0xB2 :			//unknown
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xB8 :			//unknown
		curOffset++;
		curOffset++;
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xBA :			//Reverb On
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reverb On", NULL, CLR_REVERB);
		break;

	case 0xBB :			//Reverb Off
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reverb Off", NULL, CLR_REVERB);
		break;

	//--------
	//ADSR Envelope Event
	case 0xC0 :			//ADSR Reset
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR: Reset", NULL, CLR_ADSR);
		break;

	case 0xC2 :	
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR: Attack Rate?", NULL, CLR_ADSR);
		break;

	case 0xC4 :	
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR: Sustain Rate?", NULL, CLR_ADSR);
		break;

	case 0xC5 :
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR: Release Rate?", NULL, CLR_ADSR);
		break;

	case 0xC6 :
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xC7 :
		curOffset++;
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xC8 :
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xC9 :	
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR: Decay Rate?", NULL, CLR_ADSR);
		break;

	case 0xCA :
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR: Sustain Level?", NULL, CLR_ADSR);
		break;

	//--------
	//Pitch bend Event
	case 0xD0 :			// unknown
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xD1 :			// unknown
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xD2 :			// unknown
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch Bend?", NULL, CLR_PITCHBEND);
		break;

	case 0xD4 :			//Portamento (Pitch bend slide)
		{
			uint8_t cPitchSlideTimes	= GetByte(curOffset++);		//slide times [ticks]
			uint8_t cPitchSlideDepth	= GetByte(curOffset++);		//Target Panpot
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Portamento", NULL, CLR_PORTAMENTO);
		}
		break;

	case 0xD6 :			// unknown
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Detune?", NULL, CLR_PITCHBEND);
		break;

	case 0xD7 :			// LFO Depth
		{
		uint8_t cPitchLFO_Depth	= GetByte(curOffset++);		//slide times [ticks]
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Depth (Pitch bend)", NULL, CLR_LFO);
		}
		break;

	case 0xD8 :			// LFO Length
		{
			uint8_t cPitchLFO_Decay2	= GetByte(curOffset++);		//
			uint8_t cPitchLFO_Cycle	= GetByte(curOffset++);		//
			uint8_t cPitchLFO_Decay1	= GetByte(curOffset++);		//
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Length (Pitch bend)", NULL, CLR_LFO);
		}
		break;

	case 0xD9 :			// LFO ?
		curOffset++;
		curOffset++;
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO ? (Pitch bend)", NULL, CLR_LFO);
		break;

	case 0xDB :			// unknown
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	//--------
	//Volume Event
	case 0xE0 :			// Volume
		AddVol(curOffset-2, 2, GetByte(curOffset++));
		break;

	case 0xE1 :			// add volume... add the value to the current vol.
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xE2 :			// Volume slide
		{
			uint8_t dur		= GetByte(curOffset++);		//slide duration (ticks)
			uint8_t targVol	= GetByte(curOffset++);		//target volume
			AddVolSlide(beginOffset, curOffset-beginOffset, dur, targVol);
		}
		break;

	case 0xE3 :			// LFO Depth
		{
			uint8_t cVolLFO_Depth	= GetByte(curOffset++);		//
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Depth (Volume)", NULL, CLR_LFO);
		}
		break;

	case 0xE4 :			// LFO Length
		{
			uint8_t cVolLFO_Decay2	= GetByte(curOffset++);		//
			uint8_t cVolLFO_Cycle	= GetByte(curOffset++);		//
			uint8_t cVolLFO_Decay1	= GetByte(curOffset++);		//
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Length (Volume)", NULL, CLR_LFO);
		}
		break;

	case 0xE5 :			// LFO ?
		curOffset++;
		curOffset++;
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO ? (Volume)", NULL, CLR_LFO);
		break;

	case 0xE7 :			// unknown
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	//--------
	//Panpot Event
	case 0xE8 :			// Panpot
		AddPan(curOffset-2, 2, GetByte(curOffset++));
		break;

	case 0xE9 :			// unknown
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xEA :			// Panpot slide
		{
			uint8_t dur		= GetByte(curOffset++);		//slide duration (ticks)
			uint8_t targPan	= GetByte(curOffset++);		//target panpot
			AddPanSlide(beginOffset, curOffset-beginOffset, dur, targPan);
		}
		break;

	case 0xEB :			// LFO Depth
		{
			uint8_t cPanLFO_Depth	= GetByte(curOffset++);			//
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Depth (Panpot)", NULL, CLR_LFO);
		}
		break;

	case 0xEC :			// LFO Length
		{
			uint8_t cPanLFO_Decay2	= GetByte(curOffset++);			//
			uint8_t cPanLFO_Cycle	= GetByte(curOffset++);			//
			uint8_t cPanLFO_Decay1	= GetByte(curOffset++);			//
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO Length (Panpot)", NULL, CLR_LFO);
		}
		break;

	case 0xED :			// LFO ?
		curOffset++;
		curOffset++;
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO ? (Panpot)", NULL, CLR_LFO);
		break;

	case 0xEF :			// unknown
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	//--------
	// Event
	case 0xF8 :			//unknown
		curOffset++;
		curOffset++;
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xF9 :			//unknown
		curOffset++;
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xFB :			//unknown
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xFC :			//unknown
		curOffset++;
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xFD :			//unknown
		curOffset++;
		AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xFE :			// Program(WDS) bank select
		uint8_t cProgBankNum	= GetByte(curOffset++);		//Bank Number [ticks]
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Program bank select", NULL, CLR_PROGCHANGE);
		break;

	}
	return true;
}
