/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "FFTSeq.h"
#include "FFTFormat.h"

DECLARE_FORMAT(FFT);

using namespace std;

//  ******
//  FFTSeq
//  ******


FFTSeq::FFTSeq(RawFile *file, uint32_t offset)
    : VGMSeq(FFTFormat::name, file, offset) {
  AlwaysWriteInitialVol(127);
  UseLinearAmplitudeScale();
  UseReverb();
}

FFTSeq::~FFTSeq(void) {
}

bool FFTSeq::GetHeaderInfo(void) {

//-----------------------------------------------------------
//	Written by "Sound tester 774" in "内蔵音源をMIDI変換するスレ(in http://www.2ch.net)"
//	2009. 6.17 (Wed.)
//	2009. 6.30 (Thu.)
//-----------------------------------------------------------
  unLength = GetShort(dwOffset + 0x08);
  nNumTracks = GetByte(dwOffset + 0x14);    //uint8_t (8bit)		GetWord() から修正
  uint8_t cNumPercussion = GetByte(dwOffset + 0x15);    //uint8_t (8bit)	Quantity of Percussion struct
//	unsigned char	cBankNum		= GetByte(dwOffset+0x16);
  assocWdsID = GetShort(dwOffset + 0x16);    //uint16_t (16bit)	Default program bank No.
  uint16_t ptSongTitle = GetShort(dwOffset + 0x1E);    //uint16_t (16bit)	Pointer of music title (AscII strings)
  uint16_t ptPercussionTbl = GetShort(dwOffset + 0x20);    //uint16_t (16bit)	Pointer of Percussion struct

  int titleLength = ptPercussionTbl - ptSongTitle;
  char *songtitle = new char[titleLength];
  GetBytes(dwOffset + ptSongTitle, titleLength, songtitle);
  m_name = std::string(songtitle, songtitle + titleLength);
  delete[] songtitle;

  VGMHeader *hdr = AddHeader(dwOffset, 0x22);
  hdr->AddSig(dwOffset, 4);
  hdr->AddSimpleItem(dwOffset + 0x08, 2, "Size");
  hdr->AddSimpleItem(dwOffset + 0x14, 1, "Track count");
  hdr->AddSimpleItem(dwOffset + 0x15, 1, "Drum instrument count");
  hdr->AddSimpleItem(dwOffset + 0x16, 1, "Associated Sample Set ID");
  hdr->AddSimpleItem(dwOffset + 0x1E, 2, "Song title Pointer");
  hdr->AddSimpleItem(dwOffset + 0x20, 2, "Drumkit Data Pointer");

  VGMHeader *trackPtrs = AddHeader(dwOffset + 0x22, nNumTracks * 2, "Track Pointers");
  for (unsigned int i = 0; i < nNumTracks; i++)
    trackPtrs->AddSimpleItem(dwOffset + 0x22 + i * 2, 2, "Track Pointer");
  VGMHeader *titleHdr = AddHeader(dwOffset + ptSongTitle, titleLength, "Song Name");

//	if(cNumPercussion!=0){										//これ、やっぱ、いらない。
//		hdr->AddSimpleItem(dwOffset+ptPercussionTbl, cNumPercussion*5, "Drumkit Struct");
//	}
//-----------------------------------------------------------

  SetPPQN(0x30);

//	while (j && j != '.')
//	{
////		j = GetByte(dwOffset+0x22+nNumTracks*2+2 + i++);		//修正
//		j = GetByte(dwOffset + ptSongTitle + (i++));
//		name += (char)j;
//	}


//	hdr->AddSimpleItem(dwOffset + ptMusicTile, i, "Music title");		//やっぱ書かないでいいや。


  return true;
}


bool FFTSeq::GetTrackPointers(void) {
  for (unsigned int i = 0; i < nNumTracks; i++)
    aTracks.push_back(new FFTTrack(this, GetShort(dwOffset + 0x22 + (i * 2)) + dwOffset));
  return true;
}


//  ********
//  FFTTrack
//  ********

FFTTrack::FFTTrack(FFTSeq *parentFile, long offset, long length)
    : SeqTrack(parentFile, offset, length) {
  ResetVars();
}

void FFTTrack::ResetVars() {
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
bool FFTTrack::ReadEvent(void) {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = GetByte(curOffset++);

  if (status_byte < 0x80) //then it's a note on event
  {
    uint8_t data_byte = GetByte(curOffset++);
    uint8_t vel = status_byte;                            //Velocity
    uint8_t relative_key = (data_byte / 19);                        //Note
    uint32_t iDeltaTime = delta_time_table[data_byte % 19];        //Delta Time

    if (iDeltaTime == 0)
      iDeltaTime = GetByte(curOffset++);        //Delta time
    if (bNoteOn)
      AddNoteOffNoItem(prevKey);
    AddNoteOn(beginOffset, curOffset - beginOffset, octave * 12 + relative_key, vel);
    AddTime(iDeltaTime);

    bNoteOn = true;
  }
  else
    switch (status_byte) {

      //--------
      //Rest & Tie

      //rest + noteOff
      case 0x80: {
        uint8_t restTicks = GetByte(curOffset++);
        if (bNoteOn)
          AddNoteOffNoItem(prevKey);
        AddRest(beginOffset, curOffset - beginOffset, restTicks);
        bNoteOn = false;
        break;
      }

        //hold (Tie)
      case 0x81:
        AddTime(GetByte(curOffset++));
        AddHold(beginOffset, curOffset - beginOffset, "Tie");
        break;

        //--------
        //Track Event

        //end of track
      case 0x90:
        if (infiniteLoopPt == 0) {
          AddEndOfTrack(beginOffset, curOffset - beginOffset);
          return false;
        }
        octave = infiniteLoopOctave;
        curOffset = infiniteLoopPt;
        return AddLoopForever(beginOffset, 1);

        //Dal Segno. (Infinite loop)
      case 0x91:
        infiniteLoopPt = curOffset;
        infiniteLoopOctave = octave;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Track Repeat Point", "", CLR_LOOP);
        //AddEventItem(_T("Repeat"), ICON_STARTREP, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);
        break;

        //--------
        //Octave Event

        //Set octave
      case 0x94:
        AddSetOctave(curOffset - 2, 2, GetByte(curOffset++));
        break;

        //Inc octave
      case 0x95:
        AddIncrementOctave(beginOffset, curOffset - beginOffset);
        break;

        //Dec octave
      case 0x96 :
        AddDecrementOctave(beginOffset, curOffset - beginOffset);
        break;

        //--------
        //Time signature
      case 0x97: {
        uint8_t numer = GetByte(curOffset++);
        uint8_t denom = GetByte(curOffset++);
        AddTimeSig(beginOffset, curOffset - beginOffset, numer, denom, (uint8_t) parentSeq->GetPPQN());
        break;
      }

        //--------
        //Repeat Event

        //Repeat Begin
      case 0x98: {
        bInLoop = false;
        uint8_t loopCount = GetByte(curOffset++);
        loop_layer++;
        loop_begin_loc[loop_layer] = curOffset;
        loop_counter[loop_layer] = 0;
        loop_repeats[loop_layer] = loopCount - 1;
        loop_octave[loop_layer] = octave;            //1,Sep.2009 revise
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Begin", "", CLR_LOOP);
        //AddEventItem("Loop Begin", ICON_STARTREP, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);
        break;
      }

        //Repeat End
      case 0x99:
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Repeat End", "", CLR_LOOP);
        //AddEventItem("Loop End", ICON_ENDREP, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);

        // hitting loop end for first time
        if (loopID[loop_layer] != curOffset) {
          bInLoop = true;
          loopID[loop_layer] = curOffset;
          loop_end_loc[loop_layer] = curOffset;
          curOffset = loop_begin_loc[loop_layer];
          loop_end_octave[loop_layer] = octave;        //1,Sep.2009 revise
          octave = loop_octave[loop_layer];            //1,Sep.2009 revise
          loop_counter[loop_layer]++;
        }
        else if (loop_counter[loop_layer] < loop_repeats[loop_layer]) //need to repeat loop, not first time
        {
          loop_counter[loop_layer]++;
          curOffset = loop_begin_loc[loop_layer];
          octave = loop_octave[loop_layer];            //1,Sep.2009 revise
        }
        else        //loop counter exhausted, end of loop
        {
          loopID[loop_layer] = 0;
          loop_layer--;
          if (loop_counter[loop_layer] == 0)
            bInLoop = false;
        }
        break;

        //Repeat break on last repeat
      case 0x9A:
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Break", "", CLR_LOOP);
        //AddEventItem("Loop Break", ICON_ENDREP, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);

        // if this is the last run of the loop, we break
        if (loop_counter[loop_layer] == loop_repeats[loop_layer]) {
          curOffset = loop_end_loc[loop_layer];  //jump to loop end point
          octave = loop_end_octave[loop_layer];  //set base_key to loop end base key
          loopID[loop_layer] = 0;
          loop_layer--;
          if (loop_counter[loop_layer] == 0)
            bInLoop = false;
        }
        break;

        //--------
        //General Event

        //set tempo
      case 0xA0: {
        uint8_t cTempo = (GetByte(curOffset++) * 256) / 218;
        AddTempoBPM(beginOffset, curOffset - beginOffset, cTempo);
        break;
      }

        //tempo slide
      case 0xA2: {
        uint8_t cTempoSlideTimes = GetByte(curOffset++);        //slide times [ticks]
        uint8_t cTempoSlideTarget = (GetByte(curOffset++) * 256) / 218;        //Target Panpot
        AddTempoBPM(beginOffset, curOffset - beginOffset, cTempoSlideTarget, "Tempo slide");
        break;
      }

        //unknown
      case 0xA9:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //program change
      case 0xAC: {
        uint8_t progNum = GetByte(curOffset++);
        // to do:	Bank select(op-code:0xFE)
        //			Default bank number is "assocWdsID".
        AddBankSelectNoItem(progNum / 128);
        if (progNum > 127)
          progNum %= 128;
        // When indicate 255 in progNum, program No.0 & bank No.254 is selected.
        AddProgramChange(beginOffset, curOffset - beginOffset, progNum);
        break;
      }

        //unknown
      case 0xAD:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //Percussion On
      case 0xAE :
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Percussion On", "", CLR_CHANGESTATE);
        break;

        //Percussion Off
      case 0xAF:
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Percussion Off", "", CLR_CHANGESTATE);
        break;

        //Slur On
      case 0xB0:
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Slur On", "", CLR_PORTAMENTO);
        break;

        //Slur Off
      case 0xB1:
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Slur Off", "", CLR_PORTAMENTO);
        break;

        //unknown
      case 0xB2:
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //unknown
      case 0xB8:
        curOffset++;
        curOffset++;
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //Reverb On
      case 0xBA :
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Reverb On", "", CLR_REVERB);
        break;

        //Reverb Off
      case 0xBB :
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Reverb Off", "", CLR_REVERB);
        break;

        //--------
        //ADSR Envelope Event

        //ADSR Reset
      case 0xC0:
        AddGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Reset", "", CLR_ADSR);
        break;

      case 0xC2:
        curOffset++;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Attack Rate?", "", CLR_ADSR);
        break;

      case 0xC4:
        curOffset++;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Sustain Rate?", "", CLR_ADSR);
        break;

      case 0xC5:
        curOffset++;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Release Rate?", "", CLR_ADSR);
        break;

      case 0xC6:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xC7:
        curOffset++;
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xC8 :
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xC9:
        curOffset++;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Decay Rate?", "", CLR_ADSR);
        break;

      case 0xCA:
        curOffset++;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Sustain Level?", "", CLR_ADSR);
        break;

        //--------
        //Pitch bend Event

        // unknown
      case 0xD0:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        // unknown
      case 0xD1:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        // unknown
      case 0xD2:
        curOffset++;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Bend?", "", CLR_PITCHBEND);
        break;

        //Portamento (Pitch bend slide)
      case 0xD4: {
        uint8_t cPitchSlideTimes = GetByte(curOffset++);        //slide times [ticks]
        uint8_t cPitchSlideDepth = GetByte(curOffset++);        //Target Panpot
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Portamento", "", CLR_PORTAMENTO);
        break;
      }

        // unknown
      case 0xD6:
        curOffset++;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Detune?", "", CLR_PITCHBEND);
        break;

        // LFO Depth
      case 0xD7: {
        uint8_t cPitchLFO_Depth = GetByte(curOffset++);        //slide times [ticks]
        AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO Depth (Pitch bend)", "", CLR_LFO);
        break;
      }

        // LFO Length
      case 0xD8: {
        uint8_t cPitchLFO_Decay2 = GetByte(curOffset++);
        uint8_t cPitchLFO_Cycle = GetByte(curOffset++);
        uint8_t cPitchLFO_Decay1 = GetByte(curOffset++);
        AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO Length (Pitch bend)", "", CLR_LFO);
        break;
      }

        // LFO ?
      case 0xD9:
        curOffset++;
        curOffset++;
        curOffset++;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO ? (Pitch bend)", "", CLR_LFO);
        break;

        // unknown
      case 0xDB:
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //--------
        //Volume Event

        // Volume
      case 0xE0:
        AddVol(curOffset - 2, 2, GetByte(curOffset++));
        break;

        // add volume... add the value to the current vol.
      case 0xE1:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        // Volume slide
      case 0xE2: {
        uint8_t dur = GetByte(curOffset++);        //slide duration (ticks)
        uint8_t targVol = GetByte(curOffset++);        //target volume
        AddVolSlide(beginOffset, curOffset - beginOffset, dur, targVol);
        break;
      }

        // LFO Depth
      case 0xE3: {
        uint8_t cVolLFO_Depth = GetByte(curOffset++);        //
        AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO Depth (Volume)", "", CLR_LFO);
        break;
      }

        // LFO Length
      case 0xE4: {
        uint8_t cVolLFO_Decay2 = GetByte(curOffset++);
        uint8_t cVolLFO_Cycle = GetByte(curOffset++);
        uint8_t cVolLFO_Decay1 = GetByte(curOffset++);
        AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO Length (Volume)", "", CLR_LFO);
        break;
      }

        // LFO ?
      case 0xE5:
        curOffset++;
        curOffset++;
        curOffset++;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO ? (Volume)", "", CLR_LFO);
        break;

        // unknown
      case 0xE7:
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //--------
        //Panpot Event

        // Panpot
      case 0xE8:
        AddPan(curOffset - 2, 2, GetByte(curOffset++));
        break;

        // unknown
      case 0xE9:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        // Panpot slide
      case 0xEA: {
        uint8_t dur = GetByte(curOffset++);        //slide duration (ticks)
        uint8_t targPan = GetByte(curOffset++);        //target panpot
        AddPanSlide(beginOffset, curOffset - beginOffset, dur, targPan);
        break;
      }

        // LFO Depth
      case 0xEB: {
        uint8_t cPanLFO_Depth = GetByte(curOffset++);
        AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO Depth (Panpot)", "", CLR_LFO);
        break;
      }

        // LFO Length
      case 0xEC: {
        uint8_t cPanLFO_Decay2 = GetByte(curOffset++);
        uint8_t cPanLFO_Cycle = GetByte(curOffset++);
        uint8_t cPanLFO_Decay1 = GetByte(curOffset++);
        AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO Length (Panpot)", "", CLR_LFO);
        break;
      }

        // LFO ?
      case 0xED:
        curOffset++;
        curOffset++;
        curOffset++;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "LFO ? (Panpot)", "", CLR_LFO);
        break;

        // unknown
      case 0xEF:
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //--------
        // Event

        //unknown
      case 0xF8:
        curOffset++;
        curOffset++;
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //unknown
      case 0xF9:
        curOffset++;
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //unknown
      case 0xFB:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //unknown
      case 0xFC:
        curOffset++;
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        //unknown
      case 0xFD:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

        // Program(WDS) bank select
      case 0xFE:
        uint8_t cProgBankNum = GetByte(curOffset++);        //Bank Number [ticks]
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Program bank select", "", CLR_PROGCHANGE);
        break;

    }
  return true;
}

