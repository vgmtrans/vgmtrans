#include "SquarePS2Seq.h"

DECLARE_FORMAT(SquarePS2);

using namespace std;

// ******
// BGMSeq
// ******

BGMSeq::BGMSeq(RawFile *file, uint32_t offset)
    : VGMSeq(SquarePS2Format::name, file, offset) {
  UseLinearAmplitudeScale();
  UseReverb();
  AlwaysWriteInitialVol(127);
}

bool BGMSeq::GetHeaderInfo() {
  VGMHeader *header = addHeader(dwOffset, 0x20, "Header");
  header->addChild(dwOffset, 4, "Signature");
  header->addChild(dwOffset + 0x4, 2, "ID");
  header->addChild(dwOffset + 0x6, 2, "Associated WD ID");
  header->addChild(dwOffset + 0x8, 1, "Number of Tracks");
  header->addChild(dwOffset + 0xE, 2, "PPQN");
  header->addChild(dwOffset + 0x10, 4, "File length");

  nNumTracks = GetByte(dwOffset + 8);
  seqID = GetShort(dwOffset + 4);
  assocWDID = GetShort(dwOffset + 6);
  SetPPQN(GetShort(dwOffset + 0xE));
  unLength = GetWord(dwOffset + 0x10);

  ostringstream theName;
  theName << "BGM " << seqID;
  if (seqID != assocWDID)
    theName << "using WD " << assocWDID;
  setName(theName.str());
  return true;
}

bool BGMSeq::GetTrackPointers() {
  uint32_t pos = dwOffset + 0x20;    //start at first track (fixed offset)
  for (unsigned int i = 0; i < nNumTracks; i++) {
    //HACK FOR TRUNCATED BGMS (ex. FFXII 113 Eastersand.psf2)
    if (pos >= rawFile()->size())
      return true;
    //END HACK
    uint32_t trackSize = GetWord(pos);        //get the track size (first word before track data)
    aTracks.push_back(new BGMTrack(this, pos + 4, trackSize));
    pos += trackSize + 4;                //jump to the next track
  }
  return true;
}

// ********
// BGMTrack
// ********


BGMTrack::BGMTrack(BGMSeq *parentSeq, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length) {
}


bool BGMTrack::ReadEvent(void) {
  int value1;

  uint32_t beginOffset = curOffset;
  AddTime(ReadVarLen(curOffset));

  // address range check for safety
  if (!vgmFile()->IsValidOffset(curOffset)) {
    if (readMode== ReadMode::READMODE_ADD_TO_UI) {
      L_WARN("{}: Address out of range. Conversion aborted.", vgmFile()->name());
    }
    return false;
  }

  uint8_t status_byte = GetByte(curOffset++);

  switch (status_byte) {
    //end of track
    case 0x00 :
      AddEndOfTrack(beginOffset, curOffset - beginOffset);
      return false;

    // Loop begin
    case 0x02 :
//		loop_start = j;
      //rest_time += current_delta_time;
      //if (nScanMode == MODE_SCAN)
      //	AddBGMEvent("Loop Begin", ICON_STARTREP, aBGMTracks[cur_track]->pTreeItem, offsetAtDelta, j-offsetAtDelta, BG_CLR_CYAN);
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop Begin", "", CLR_LOOP);
      break;

    // Loop end
    case 0x03 :
//		loop_end = j;
//		loop_counter++;
      //if (loop_counter < num_loops)
      //	j = loop_start;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", "", CLR_LOOP);
      break;

    //end of track?
    case 0x04 :
      AddUnknown(beginOffset, curOffset - beginOffset);
      break;

    //set tempo
    case 0x08 : {
      uint8_t bpm = GetByte(curOffset++);
      AddTempoBPM(beginOffset, curOffset - beginOffset, bpm);
      break;
    }

    case 0x0A :
    case 0x0D :        //no idea... found at beginning of track
    case 0x28 :        //no idea.  ffxi 128.bgm
    case 0x31 :        //in FFXII prologue
    case 0x34 :
    case 0x35 :        //no clue, found in 108.bgm
    case 0x3E :        //no clue, found in 104.bgm of kingdomhearts - traverse town
    case 0x58 :
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset);
      break;

    //time signature?
    case 0x0C :
    {
      uint8_t numer = GetByte(curOffset++);
      uint8_t denom = GetByte(curOffset++);
      AddTimeSig(beginOffset, curOffset - beginOffset, numer, denom, (uint8_t) parentSeq->GetPPQN());

      //for (value3 = 0; ((value2&1) != TRUE) && (value3 < 8); ++value3)	//while
      //	value2 >>= 1;
      //RecordMidiTimeSig(current_delta_time, value1, value3, usiPPQN/4, 8, hFile);
      break;
    }

    //play previous key, no velocity param
    case 0x10 :
      AddNoteOn(beginOffset, curOffset - beginOffset, prevKey, prevVel);
      break;

    //key on with velocity
    case 0x11 : {
      auto key = GetByte(curOffset++);
      auto vel = GetByte(curOffset++);
      AddNoteOn(beginOffset, curOffset - beginOffset, key, vel);
      break;
    }

    //key on with prev velocity
    case 0x12 : {
      auto key = GetByte(curOffset++);
      AddNoteOn(beginOffset, curOffset - beginOffset, key, prevVel);
      break;
    }

    // play previous key with velocity param
    case 0x13 : {
      auto vel = GetByte(curOffset++);
      AddNoteOn(beginOffset, curOffset - beginOffset, prevKey, vel);
      break;
    }

    case 0x18 : {
      AddNoteOff(beginOffset, curOffset - beginOffset, prevKey, "Note off (prev key)");
      break;
    }

    case 0x19 :
      curOffset += 2;
      AddUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0x1A : {
      auto key = GetByte(curOffset++);
      AddNoteOff(beginOffset, curOffset - beginOffset, key);
      prevKey = key;
      break;
    }

    // program change
    case 0x20 : {
      uint8_t progNum = GetByte(curOffset++);
      AddProgramChange(beginOffset, curOffset - beginOffset, progNum);
      break;
    }

    //set volume
    case 0x22 :
      vol = GetByte(curOffset++);            //expression value
      //I am positive that volume values in this driver do not align with standard MIDI. (see FFXI 213 Ru'Lude Gardens.psf2 for example)
      AddVol(beginOffset, curOffset - beginOffset, vol);
      break;

    //expression
    case 0x24 : {
      uint8_t expression = GetByte(curOffset++);            //expression value
      AddExpression(beginOffset, curOffset - beginOffset, expression);
      break;
    }

    //pan?
    case 0x26 : {
      uint8_t pan = GetByte(curOffset++);
      AddPan(beginOffset, curOffset - beginOffset, pan);
      break;
    }

    //Sustain Pedal
    case 0x3C :
      value1 = GetByte(curOffset++);
      AddSustainEvent(beginOffset, curOffset - beginOffset, value1);
      break;

    case 0x40 :        //found in ffxi 208.bgm couple times
    case 0x48 :        //found in ffx-2 34.bgm - Yuna's Theme
    case 0x50 :        //found in bgm 048 of FFX-2, that's ost song "101 Eternity ~Memory of Lightwaves~.psf2"
      curOffset += 3;
      AddUnknown(beginOffset, curOffset - beginOffset);
      break;

    //found in ffxi 208.bgm couple times
    case 0x47 :
      curOffset += 2;
      AddUnknown(beginOffset, curOffset - beginOffset);
      break;

    //pitch bend		I SHOULD GO BACK AND VERIFY THE RANGE OF THE PITCH BEND
    case 0x5C : {
      uint8_t lsb = GetByte(curOffset++);
      uint8_t msb = GetByte(curOffset++);
      AddPitchBendMidiFormat(beginOffset, curOffset - beginOffset, lsb, msb);
      break;
    }

    //Portamento?  found in ffxi 128.bgm
    case 0x5D :
      curOffset++;
      break;

    case 0x60 :        //no clue, part of init?
    case 0x61 :        //no clue, part of init?
    case 0x7F :        //no clue, part of init?
      AddUnknown(beginOffset, curOffset - beginOffset);
      break;

    default :
      AddGenericEvent(beginOffset, curOffset - beginOffset, "UNKNOWN", "", CLR_UNRECOGNIZED);
      break;

  }
  return true;
}