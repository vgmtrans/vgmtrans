#include "pch.h"
#include "KonamiGXSeq.h"

DECLARE_FORMAT(KonamiGX);

using namespace std;

// ***********
// KonamiGXSeq
// ***********

KonamiGXSeq::KonamiGXSeq(RawFile *file, uint32_t offset)
    : VGMSeq(KonamiGXFormat::name, file, offset) {
  UseReverb();
  AlwaysWriteInitialVol(127);
}

KonamiGXSeq::~KonamiGXSeq(void) {
}

bool KonamiGXSeq::GetHeaderInfo(void) {
  //nNumTracks = GetByte(dwOffset+8);
  SetPPQN(0x30);

  ostringstream theName;
  theName << "Konami GX Seq";
  name = theName.str();
  return true;
}

bool KonamiGXSeq::GetTrackPointers(void) {
  uint32_t pos = dwOffset;
  for (int i = 0; i < 17; i++) {
    uint32_t trackOffset = GetWordBE(pos);
    if (GetByte(trackOffset) == 0xFF)    // skip empty tracks. don't even bother making them tracks
      continue;
    nNumTracks++;
    aTracks.push_back(new KonamiGXTrack(this, trackOffset, 0));
    pos += 4;
  }
  return true;
}

//bool KonamiGXSeq::LoadTracks(void)
//{
//	for (uint32_t i=0; i<nNumTracks; i++)
//	{
//		if (!aTracks[i]->LoadTrack(i, 0xFFFFFFFF))
//			return false;
//	}
//	aTracks[0]->InsertTimeSig(0, 0, 4, 4, GetPPQN(), 0);
//}

// *************
// KonamiGXTrack
// *************


KonamiGXTrack::KonamiGXTrack(KonamiGXSeq *parentSeq, long offset, long length)
    : SeqTrack(parentSeq, offset, length), bInJump(false) {
}


// I'm going to try to follow closely to the original Salamander 2 code at 0x30C6
bool KonamiGXTrack::ReadEvent(void) {
  uint32_t beginOffset = curOffset;
  uint32_t deltatest = GetTime();
  //AddDelta(ReadVarLen(curOffset));

  uint8_t status_byte = GetByte(curOffset++);

  if (status_byte == 0xFF) {
    if (bInJump) {
      bInJump = false;
      curOffset = jump_return_offset;
    }
    else {
      AddEndOfTrack(beginOffset, curOffset - beginOffset);
      return 0;
    }
  }
  else if (status_byte == 0x60) {
    return 1;
  }
  else if (status_byte == 0x61) {
    return 1;
  }
  else if (status_byte < 0xC0)    //note event
  {
    uint8_t note, delta;
    if (status_byte < 0x62) {
      delta = GetByte(curOffset++);
      note = status_byte;
      prevDelta = delta;
    }
    else {
      delta = prevDelta;
      note = status_byte - 0x62;
    }

    uint8_t nextDataByte = GetByte(curOffset++);
    uint8_t dur, vel;
    if (nextDataByte < 0x80) {
      dur = nextDataByte;
      prevDur = dur;
      vel = GetByte(curOffset++);
    }
    else {
      dur = prevDur;
      vel = nextDataByte - 0x80;
    }

    //AddNoteOn(beginOffset, curOffset-beginOffset, note, vel);
    //AddDelta(dur);
    //AddNoteOffNoItem(note);
    uint32_t newdur = (delta * dur) / 0x64;
    if (newdur == 0)
      newdur = 1;
    AddNoteByDur(beginOffset, curOffset - beginOffset, note, vel, newdur);
    AddTime(delta);
//		if (newdur > delta)
//			ATLTRACE("newdur > delta.  %X > %X.  occurring at %X\n", newdur, delta, beginOffset);
    //AddDelta(dur);
  }
  else
    switch (status_byte) {
      case 0xC0:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xCE:
        curOffset += 2;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xD0:
        curOffset += 3;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xD1:
      case 0xD2:
      case 0xD3:
      case 0xD4:
      case 0xD5:
      case 0xD6:
        curOffset += 2;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xDE:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      //Rest
      case 0xE0: {
        uint8_t delta = GetByte(curOffset++);
        AddTime(delta);
        break;
      }

      //Hold
      case 0xE1: {
        uint8_t delta = GetByte(curOffset++);
        uint8_t dur = GetByte(curOffset++);
        uint32_t newdur = (delta * dur) / 0x64;
        AddTime(newdur);
        this->MakePrevDurNoteEnd();
        AddTime(delta - newdur);
        break;
      }

      //program change
      case 0xE2: {
        uint8_t progNum = GetByte(curOffset++);
        AddProgramChange(beginOffset, curOffset - beginOffset, progNum);
        break;
      }

      //stereo related
      case 0xE3:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE4:
      case 0xE5:
        curOffset += 3;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE6:
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE7:
        curOffset += 3;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE8:
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE9:
        curOffset += 3;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      //tempo
      case 0xEA: {
        uint8_t bpm = GetByte(curOffset++);
        AddTempoBPM(beginOffset, curOffset - beginOffset, bpm);
        break;
      }

      case 0xEC:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      //master vol
      case 0xEE: {
        uint8_t vol = GetByte(curOffset++);
        //AddMasterVol(beginOffset, curOffset-beginOffset, vol);
        break;
      }

      case 0xEF:
        curOffset += 2;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF0:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF1:
        curOffset += 3;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF2:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF3:
        curOffset += 3;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF6:
      case 0xF7:
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF8:
        curOffset += 2;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      //release rate
      case 0xFA:
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xFD:
        curOffset += 4;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop", "", CLR_LOOP);
        break;

      case 0xFE:
        bInJump = true;
        jump_return_offset = curOffset + 4;
        AddGenericEvent(beginOffset, jump_return_offset - beginOffset, "Jump", "", CLR_LOOP);
        curOffset = GetWordBE(curOffset);
        //if (curOffset > 0x100000)
        //	return 0;
        break;

      default:
        AddEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;
    }
  return true;
}