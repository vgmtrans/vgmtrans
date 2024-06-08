#include "KonamiGXSeq.h"

DECLARE_FORMAT(KonamiGX);

using namespace std;

// ***********
// KonamiGXSeq
// ***********

KonamiGXSeq::KonamiGXSeq(RawFile *file, uint32_t offset)
    : VGMSeq(KonamiGXFormat::name, file, offset, 0, "Konami GX Seq") {
  useReverb();
  setAlwaysWriteInitialVol(127);
}

KonamiGXSeq::~KonamiGXSeq(void) {
}

bool KonamiGXSeq::parseHeader(void) {
  //nNumTracks = GetByte(dwOffset+8);
  setPPQN(0x30);
  return true;
}

bool KonamiGXSeq::parseTrackPointers(void) {
  uint32_t pos = dwOffset;
  for (int i = 0; i < 17; i++) {
    uint32_t trackOffset = readWordBE(pos);
    if (readByte(trackOffset) == 0xFF)    // skip empty tracks. don't even bother making them tracks
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


KonamiGXTrack::KonamiGXTrack(KonamiGXSeq *parentSeq, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length), bInJump(false) {
}


// I'm going to try to follow closely to the original Salamander 2 code at 0x30C6
bool KonamiGXTrack::readEvent(void) {
  uint32_t beginOffset = curOffset;
  uint32_t deltatest = getTime();
  //AddDelta(ReadVarLen(curOffset));

  uint8_t status_byte = readByte(curOffset++);

  if (status_byte == 0xFF) {
    if (bInJump) {
      bInJump = false;
      curOffset = jump_return_offset;
    }
    else {
      addEndOfTrack(beginOffset, curOffset - beginOffset);
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
      delta = readByte(curOffset++);
      note = status_byte;
      prevDelta = delta;
    }
    else {
      delta = prevDelta;
      note = status_byte - 0x62;
    }

    uint8_t nextDataByte = readByte(curOffset++);
    uint8_t dur, vel;
    if (nextDataByte < 0x80) {
      dur = nextDataByte;
      prevDur = dur;
      vel = readByte(curOffset++);
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
    addNoteByDur(beginOffset, curOffset - beginOffset, note, vel, newdur);
    addTime(delta);
//		if (newdur > delta)
//			ATLTRACE("newdur > delta.  %X > %X.  occurring at %X\n", newdur, delta, beginOffset);
    //AddDelta(dur);
  }
  else
    switch (status_byte) {
      case 0xC0:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xCE:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xD0:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xD1:
      case 0xD2:
      case 0xD3:
      case 0xD4:
      case 0xD5:
      case 0xD6:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xDE:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //Rest
      case 0xE0: {
        uint8_t delta = readByte(curOffset++);
        addTime(delta);
        break;
      }

      //Hold
      case 0xE1: {
        uint8_t delta = readByte(curOffset++);
        uint8_t dur = readByte(curOffset++);
        uint32_t newdur = (delta * dur) / 0x64;
        addTime(newdur);
        this->makePrevDurNoteEnd();
        addTime(delta - newdur);
        break;
      }

      //program change
      case 0xE2: {
        uint8_t progNum = readByte(curOffset++);
        addProgramChange(beginOffset, curOffset - beginOffset, progNum);
        break;
      }

      //stereo related
      case 0xE3:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE4:
      case 0xE5:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE6:
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE7:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE8:
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE9:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //tempo
      case 0xEA: {
        uint8_t bpm = readByte(curOffset++);
        addTempoBPM(beginOffset, curOffset - beginOffset, bpm);
        break;
      }

      case 0xEC:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //master vol
      case 0xEE: {
        uint8_t vol = readByte(curOffset++);
        //AddMasterVol(beginOffset, curOffset-beginOffset, vol);
        break;
      }

      case 0xEF:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF0:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF1:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF2:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF3:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF6:
      case 0xF7:
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF8:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      //release rate
      case 0xFA:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xFD:
        curOffset += 4;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Loop", "", CLR_LOOP);
        break;

      case 0xFE:
        bInJump = true;
        jump_return_offset = curOffset + 4;
        addGenericEvent(beginOffset, jump_return_offset - beginOffset, "Jump", "", CLR_LOOP);
        curOffset = getWordBE(curOffset);
        //if (curOffset > 0x100000)
        //	return 0;
        break;

      default:
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;
    }
  return true;
}