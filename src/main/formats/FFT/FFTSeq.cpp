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
  setAlwaysWriteInitialVol(127);
  setUseLinearAmplitudeScale(true);
  useReverb();
}

FFTSeq::~FFTSeq(void) {
}

bool FFTSeq::parseHeader(void) {

//-----------------------------------------------------------
//	Written by "Sound tester 774" in "内蔵音源をMIDI変換するスレ(in http://www.2ch.net)"
//	2009. 6.17 (Wed.)
//	2009. 6.30 (Thu.)
//-----------------------------------------------------------
  unLength = readShort(dwOffset + 0x08);
  nNumTracks = readByte(dwOffset + 0x14);    //uint8_t (8bit)		GetWord() から修正
  // uint8_t cNumPercussion = GetByte(dwOffset + 0x15);    //uint8_t (8bit)	Quantity of Percussion struct
  assocWdsID = readShort(dwOffset + 0x16);    //uint16_t (16bit)	Default program bank No.
  uint16_t ptSongTitle = readShort(dwOffset + 0x1E);    //uint16_t (16bit)	Pointer of music title (AscII strings)
  uint16_t ptPercussionTbl = readShort(dwOffset + 0x20);    //uint16_t (16bit)	Pointer of Percussion struct

  int titleLength = ptPercussionTbl - ptSongTitle;
  char *songtitle = new char[titleLength];
  readBytes(dwOffset + ptSongTitle, titleLength, songtitle);
  setName(std::string(songtitle, songtitle + titleLength));
  delete[] songtitle;

  VGMHeader *hdr = addHeader(dwOffset, 0x22);
  hdr->addSig(dwOffset, 4);
  hdr->addChild(dwOffset + 0x08, 2, "Size");
  hdr->addChild(dwOffset + 0x14, 1, "Track count");
  hdr->addChild(dwOffset + 0x15, 1, "Drum instrument count");
  hdr->addChild(dwOffset + 0x16, 1, "Associated Sample Set ID");
  hdr->addChild(dwOffset + 0x1E, 2, "Song title Pointer");
  hdr->addChild(dwOffset + 0x20, 2, "Drumkit Data Pointer");

  VGMHeader *trackPtrs = addHeader(dwOffset + 0x22, nNumTracks * 2, "Track Pointers");
  for (unsigned int i = 0; i < nNumTracks; i++)
    trackPtrs->addChild(dwOffset + 0x22 + i * 2, 2, "Track Pointer");
  addHeader(dwOffset + ptSongTitle, titleLength, "Song Name");

//	if(cNumPercussion!=0){										//これ、やっぱ、いらない。
//		hdr->addSimpleChild(dwOffset+ptPercussionTbl, cNumPercussion*5, "Drumkit Struct");
//	}
//-----------------------------------------------------------

  setPPQN(0x30);

//	while (j && j != '.')
//	{
////		j = GetByte(dwOffset+0x22+nNumTracks*2+2 + i++);		//修正
//		j = GetByte(dwOffset + ptSongTitle + (i++));
//		name += (char)j;
//	}


//	hdr->addSimpleChild(dwOffset + ptMusicTile, i, "Music title");		//やっぱ書かないでいいや。


  return true;
}


bool FFTSeq::parseTrackPointers(void) {
  for (unsigned int i = 0; i < nNumTracks; i++)
    aTracks.push_back(new FFTTrack(this, readShort(dwOffset + 0x22 + (i * 2)) + dwOffset));
  return true;
}


//  ********
//  FFTTrack
//  ********

FFTTrack::FFTTrack(FFTSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  FFTTrack::resetVars();
}

void FFTTrack::resetVars() {
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
  SeqTrack::resetVars();

  octave = 3;
}


//--------------------------------------------------
//Revisions:
//	2009. 6.17(Wed.) :	Re-make by "Sound tester 774" in "内蔵音源をMIDI変換するスレ(in http://www.2ch.net)"
//						Add un-known command(op-code).
//--------------------------------------------------
bool FFTTrack::readEvent() {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);

  std::string desc;

  if (status_byte < 0x80) //then it's a note on event
  {
    uint8_t data_byte = readByte(curOffset++);
    uint8_t vel = status_byte;                            //Velocity
    uint8_t relative_key = (data_byte / 19);                        //Note
    uint32_t iDeltaTime = delta_time_table[data_byte % 19];        //Delta Time

    if (iDeltaTime == 0)
      iDeltaTime = readByte(curOffset++);        //Delta time
    if (bNoteOn)
      addNoteOffNoItem(prevKey);
    addNoteOn(beginOffset, curOffset - beginOffset, octave * 12 + relative_key, vel);
    addTime(iDeltaTime);

    bNoteOn = true;
  }
  else
    switch (status_byte) {

      //--------
      //Rest & Tie

      //rest + noteOff
      case 0x80: {
        uint8_t restTicks = readByte(curOffset++);
        if (bNoteOn)
          addNoteOffNoItem(prevKey);
        addRest(beginOffset, curOffset - beginOffset, restTicks);
        bNoteOn = false;
        break;
      }

        //hold (Tie)
      case 0x81:
        addTime(readByte(curOffset++));
        addHold(beginOffset, curOffset - beginOffset, "Tie");
        break;

        //--------
        //Track Event

        //end of track
      case 0x90:
        if (infiniteLoopPt == 0) {
          addEndOfTrack(beginOffset, curOffset - beginOffset);
          return false;
        }
        octave = infiniteLoopOctave;
        curOffset = infiniteLoopPt;
        return addLoopForever(beginOffset, 1);

        //Dal Segno. (Infinite loop)
      case 0x91:
        infiniteLoopPt = curOffset;
        infiniteLoopOctave = octave;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Track Repeat Point", "", CLR_LOOP);
        //AddEventItem(_T("Repeat"), ICON_STARTREP, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);
        break;

        //--------
        //Octave Event

        //Set octave
      case 0x94: {
        uint8_t newOctave = readByte(curOffset++);
        addSetOctave(beginOffset, curOffset - beginOffset, newOctave);
        break;
      }

        //Inc octave
      case 0x95:
        addIncrementOctave(beginOffset, curOffset - beginOffset);
        break;

        //Dec octave
      case 0x96 :
        addDecrementOctave(beginOffset, curOffset - beginOffset);
        break;

        //--------
        //Time signature
      case 0x97: {
        uint8_t numer = readByte(curOffset++);
        uint8_t denom = readByte(curOffset++);
        addTimeSig(beginOffset, curOffset - beginOffset, numer, denom, static_cast<uint8_t>(parentSeq->ppqn()));
        break;
      }

        //--------
        //Repeat Event

        //Repeat Begin
      case 0x98: {
        bInLoop = false;
        uint8_t loopCount = readByte(curOffset++);
        loop_layer++;
        loop_begin_loc[loop_layer] = curOffset;
        loop_counter[loop_layer] = 0;
        loop_repeats[loop_layer] = loopCount - 1;
        loop_octave[loop_layer] = octave;            //1,Sep.2009 revise
        addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Begin", "", CLR_LOOP);
        //AddEventItem("Loop Begin", ICON_STARTREP, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);
        break;
      }

        //Repeat End
      case 0x99:
        addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat End", "", CLR_LOOP);
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
        addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Break", "", CLR_LOOP);
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
        uint8_t cTempo = (readByte(curOffset++) * 256) / 218;
        addTempoBPM(beginOffset, curOffset - beginOffset, cTempo);
        break;
      }

        //tempo slide
      case 0xA2: {
        uint8_t cTempoSlideTimes = readByte(curOffset++);        //slide times [ticks]
        uint8_t cTempoSlideTarget = (readByte(curOffset++) * 256) / 218;        //Target Panpot
        L_INFO("Found tempo slide event at {:X}. Not fully implemented. Duration: {:d}  Target BPM: {:d}", curOffset - 2, cTempoSlideTimes, cTempoSlideTarget);
        addTempoBPM(beginOffset, curOffset - beginOffset, cTempoSlideTarget, "Tempo slide");
        break;
      }

        //unknown
      case 0xA9:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

        //program change
      case 0xAC: {
        uint8_t progNum = readByte(curOffset++);
        // to do:	Bank select(op-code:0xFE)
        //			Default bank number is "assocWdsID".
        addBankSelectNoItem(progNum / 128);
        if (progNum > 127)
          progNum %= 128;
        // When indicate 255 in progNum, program No.0 & bank No.254 is selected.
        addProgramChange(beginOffset, curOffset - beginOffset, progNum);
        break;
      }

        //unknown
      case 0xAD:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

        //Percussion On
      case 0xAE :
        addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion On", "", CLR_CHANGESTATE);
        break;

        //Percussion Off
      case 0xAF:
        addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion Off", "", CLR_CHANGESTATE);
        break;

        //Slur On
      case 0xB0:
        addGenericEvent(beginOffset, curOffset - beginOffset, "Slur On", "", CLR_PORTAMENTO);
        break;

        //Slur Off
      case 0xB1:
        addGenericEvent(beginOffset, curOffset - beginOffset, "Slur Off", "", CLR_PORTAMENTO);
        break;

        //unknown
      case 0xB2:
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

        //unknown
      case 0xB8: {
        const auto arg1 = readByte(curOffset++);
        const auto arg2 = readByte(curOffset++);
        const auto arg3 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1, arg2, arg3);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

        //Reverb On
      case 0xBA :
        addGenericEvent(beginOffset, curOffset - beginOffset, "Reverb On", "", CLR_REVERB);
        break;

        //Reverb Off
      case 0xBB :
        addGenericEvent(beginOffset, curOffset - beginOffset, "Reverb Off", "", CLR_REVERB);
        break;

        //--------
        //ADSR Envelope Event

        //ADSR Reset
      case 0xC0:
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Reset", "", CLR_ADSR);
        break;

      case 0xC2: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Attack Rate?", desc, CLR_ADSR);
        break;
      }

      case 0xC4: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Sustain Rate?", desc, CLR_ADSR);
        break;
      }

      case 0xC5: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Release Rate?", desc, CLR_ADSR);
        break;
      }

      case 0xC6: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

      case 0xC7: {
        const auto arg1 = readByte(curOffset++);
        const auto arg2 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1, arg2);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

      case 0xC8 : {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

      case 0xC9: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Decay Rate?", desc, CLR_ADSR);
        break;
      }

      case 0xCA: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR: Sustain Level?", desc, CLR_ADSR);
        break;
      }

        //--------
        //Pitch bend Event

        // unknown
      case 0xD0: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

        // unknown
      case 0xD1: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

        // unknown
      case 0xD2: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Bend?", desc, CLR_PITCHBEND);
        break;
      }

        //Portamento (Pitch bend slide)
      case 0xD4: {
        uint8_t cPitchSlideTimes = readByte(curOffset++);        //slide times [ticks]
        uint8_t cPitchSlideDepth = readByte(curOffset++);        //Target Panpot
        desc = fmt::format("Duration: {:d} ticks  Target: {:d}", cPitchSlideTimes, cPitchSlideDepth);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento", desc, CLR_PORTAMENTO);
        break;
      }

        // unknown
      case 0xD6: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Detune?", desc, CLR_PITCHBEND);
        break;
      }

        // LFO Depth
      case 0xD7: {
        uint8_t cPitchLFO_Depth = readByte(curOffset++);        //slide times [ticks]
        desc = fmt::format("Depth: {:d}", cPitchLFO_Depth);
        addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Depth (Pitch bend)", desc, CLR_LFO);
        break;
      }

        // LFO Length
      case 0xD8: {
        uint8_t cPitchLFO_Decay2 = readByte(curOffset++);
        uint8_t cPitchLFO_Cycle = readByte(curOffset++);
        uint8_t cPitchLFO_Decay1 = readByte(curOffset++);
        desc = fmt::format("decay2: {:d}  cycle: {:d}  decay1: {:d}", cPitchLFO_Decay2, cPitchLFO_Cycle, cPitchLFO_Decay1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Length (Pitch bend)", desc, CLR_LFO);
        break;
      }

        // LFO ?
      case 0xD9: {
        const auto arg1 = readByte(curOffset++);
        const auto arg2 = readByte(curOffset++);
        const auto arg3 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1, arg2, arg3);
        addGenericEvent(beginOffset, curOffset - beginOffset, "LFO ? (Pitch bend)", desc, CLR_LFO);
        break;
      }

        // unknown
      case 0xDB:
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

        //--------
        //Volume Event

        // Volume
      case 0xE0: {
        uint8_t volByte = readByte(curOffset++);
        addVol(beginOffset, curOffset - beginOffset, volByte);
        break;
      }

        // add volume... add the value to the current vol.
      case 0xE1: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

        // Volume slide
      case 0xE2: {
        uint8_t dur = readByte(curOffset++);        //slide duration (ticks)
        uint8_t targVol = readByte(curOffset++);        //target volume
        addVolSlide(beginOffset, curOffset - beginOffset, dur, targVol);
        break;
      }

        // LFO Depth
      case 0xE3: {
        uint8_t cVolLFO_Depth = readByte(curOffset++);        //
        desc = fmt::format("Depth: {:d}", cVolLFO_Depth);
        addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Depth (Volume)", desc, CLR_LFO);
        break;
      }

        // LFO Length
      case 0xE4: {
        uint8_t cVolLFO_Decay2 = readByte(curOffset++);
        uint8_t cVolLFO_Cycle = readByte(curOffset++);
        uint8_t cVolLFO_Decay1 = readByte(curOffset++);
        desc = fmt::format("decay2: {:d}  cycle: {:d}  decay1: {:d}", cVolLFO_Decay2, cVolLFO_Cycle, cVolLFO_Decay1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Length (Volume)", desc, CLR_LFO);
        break;
      }

        // LFO ?
      case 0xE5: {
        const auto arg1 = readByte(curOffset++);
        const auto arg2 = readByte(curOffset++);
        const auto arg3 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1, arg2, arg3);
        addGenericEvent(beginOffset, curOffset - beginOffset, "LFO ? (Volume)", desc, CLR_LFO);
        break;
      }

        // unknown
      case 0xE7:
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

        //--------
        //Panpot Event

        // Panpot
      case 0xE8: {
        uint8_t panpotByte = readByte(curOffset++);
        addPan(beginOffset, curOffset - beginOffset, panpotByte);
        break;
      }

        // unknown
      case 0xE9: {
        uint8_t arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

        // Panpot slide
      case 0xEA: {
        uint8_t dur = readByte(curOffset++);        //slide duration (ticks)
        uint8_t targPan = readByte(curOffset++);        //target panpot
        addPanSlide(beginOffset, curOffset - beginOffset, dur, targPan);
        break;
      }

        // LFO Depth
      case 0xEB: {
        uint8_t cPanLFO_Depth = readByte(curOffset++);
        desc = fmt::format("Depth: {:d}", cPanLFO_Depth);
        addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Depth (Panpot)", desc, CLR_LFO);
        break;
      }

        // LFO Length
      case 0xEC: {
        uint8_t cPanLFO_Decay2 = readByte(curOffset++);
        uint8_t cPanLFO_Cycle = readByte(curOffset++);
        uint8_t cPanLFO_Decay1 = readByte(curOffset++);
        desc = fmt::format("decay2: {:d}  cycle: {:d}  decay1: {:d}", cPanLFO_Decay2, cPanLFO_Cycle, cPanLFO_Decay1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Length (Panpot)", desc, CLR_LFO);
        break;
      }

        // LFO ?
      case 0xED: {
        const auto arg1 = readByte(curOffset++);
        const auto arg2 = readByte(curOffset++);
        const auto arg3 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1, arg2, arg3);
        addGenericEvent(beginOffset, curOffset - beginOffset, "LFO ? (Panpot)", desc, CLR_LFO);
        break;
      }

        // unknown
      case 0xEF:
        desc = describeUnknownEvent(status_byte);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;

        //--------
        // Event

        //unknown
      case 0xF8: {
        const auto arg1 = readByte(curOffset++);
        const auto arg2 = readByte(curOffset++);
        const auto arg3 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1, arg2, arg3);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

        //unknown
      case 0xF9: {
        const auto arg1 = readByte(curOffset++);
        const auto arg2 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1, arg2);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

        //unknown
      case 0xFB: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

        //unknown
      case 0xFC: {
        const auto arg1 = readByte(curOffset++);
        const auto arg2 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1, arg2);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

        //unknown
      case 0xFD: {
        const auto arg1 = readByte(curOffset++);
        desc = describeUnknownEvent(status_byte, arg1);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

        // Program(WDS) bank select
      case 0xFE: {
        uint8_t cProgBankNum = readByte(curOffset++);        //Bank Number [ticks]
        desc = fmt::format(" Bank: {:d}", cProgBankNum);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Program bank select", desc, CLR_PROGCHANGE);
        break;
      }

      default:
        break;
    }
  return true;
}

