#include "util/types.h"
#include "NDSSeq.h"

DECLARE_FORMAT(NDS);

using namespace std;

NDSSeq::NDSSeq(RawFile *file, u32 offset, u32 length, string name)
    : VGMSeq(NDSFormat::name, file, offset, length, name) {
  setShouldTrackControlFlowState(true);
}

bool NDSSeq::parseHeader(void) {
  VGMHeader *SSEQHdr = addHeader(offset(), 0x10, "SSEQ Chunk Header");
  SSEQHdr->addSig(offset(), 8);
  SSEQHdr->addChild(offset() + 8, 4, "Size");
  SSEQHdr->addChild(offset() + 12, 2, "Header Size");
  SSEQHdr->addUnknownChild(offset() + 14, 2);
  //SeqChunkHdr->addSimpleChild(offset(), 4, "Blah");
  setLength(readWord(offset() + 8));
  setPPQN(0x30);
  return true;        //successful
}

bool NDSSeq::parseTrackPointers(void) {
  VGMHeader *DATAHdr = addHeader(offset() + 0x10, 0xC, "DATA Chunk Header");
  DATAHdr->addSig(offset() + 0x10, 4);
  DATAHdr->addChild(offset() + 0x10 + 4, 4, "Size");
  DATAHdr->addChild(offset() + 0x10 + 8, 4, "Data Pointer");
  u32 off = offset() + 0x1C;
  u8 b = readByte(off);
  aTracks.push_back(new NDSTrack(this));

  //FE XX XX signifies multiple tracks, each true bit in the XX values signifies there is a track for that channel
  if (b == 0xFE)
  {
    VGMHeader *TrkPtrs = addHeader(off, 0, "Track Pointers");
    TrkPtrs->addChild(off, 3, "Valid Tracks");
    off += 3;    //but all we need to do is check for subsequent 0x93 track pointer events
    b = readByte(off);
    u32 songDelay = 0;

    while (b == 0x80) {
      u32 value;
      u8 c;
      u32 beginOffset = off;
      off++;
      if ((value = readByte(off++)) & 0x80) {
        value &= 0x7F;
        do {
          value = (value << 7) + ((c = readByte(off++)) & 0x7F);
        } while (c & 0x80);
      }
      songDelay += value;
      TrkPtrs->addChild(beginOffset, off - beginOffset, "Delay");
      //songDelay += SeqTrack::ReadVarLen(++offset);
      b = readByte(off);
      break;
    }

    //Track/Channel assignment and pointer.  Channel # is irrelevant
    while (b == 0x93)
    {
      TrkPtrs->addChild(off, 5, "Track Pointer");
      u32 trkOffset = readByte(off + 2) + (readByte(off + 3) << 8) +
          (readByte(off + 4) << 16) + offset() + 0x1C;
      NDSTrack *newTrack = new NDSTrack(this, trkOffset);
      aTracks.push_back(newTrack);
      //newTrack->
      off += 5;
      b = readByte(off);
    }
    TrkPtrs->setLength(off - TrkPtrs->offset());
  }
  aTracks[0]->setOffset(off);
  aTracks[0]->dwStartOffset = off;
  return true;
}

//  ********
//  NDSTrack
//  ********

NDSTrack::NDSTrack(NDSSeq *parentFile, u32 offset, u32 length)
    : SeqTrack(parentFile, offset, length) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
}

void NDSTrack::resetVars() {
  SeqTrack::resetVars();
  noteWithDelta = false;
}

bool NDSTrack::readEvent(void) {
  u32 beginOffset = curOffset;
  u8 status_byte = readByte(curOffset++);

  if (status_byte < 0x80) //then it's a note on event
  {
    u8 vel = readByte(curOffset++);
    dur = readVarLen(curOffset);//GetByte(curOffset++);
    addNoteByDur(beginOffset, curOffset - beginOffset, status_byte, vel, dur);
    if (noteWithDelta) {
      addTime(dur);
    }
  }
  else
    switch (status_byte) {
      case 0x80:
        dur = readVarLen(curOffset);
        addRest(beginOffset, curOffset - beginOffset, dur);
        break;

      case 0x81: {
        u8 newProg = (u8) readVarLen(curOffset);
        addProgramChange(beginOffset, curOffset - beginOffset, newProg);
        break;
      }

      // [loveemu] open track, however should not handle in this function
      case 0x93:
        curOffset += 4;
        addUnknown(beginOffset, curOffset - beginOffset, "Open Track");
        break;

      case 0x94: {
        u32 jumpAddr = readByte(curOffset) + (readByte(curOffset + 1) << 8)
            + (readByte(curOffset + 2) << 16) + parentSeq->offset() + 0x1C;
        curOffset += 3;

        return addJump(beginOffset, curOffset - beginOffset, jumpAddr);
      }

      case 0x95: {
        u32 destination = readByte(curOffset) + (readByte(curOffset + 1) << 8)
            + (readByte(curOffset + 2) << 16) + parentSeq->offset() + 0x1C;
        curOffset += 3;
        u32 returnOffset = curOffset;
        return addCall(beginOffset, curOffset - beginOffset, destination, returnOffset, "Call");
      }

      // [loveemu] (ex: Hanjuku Hero DS: NSE_45, New Mario Bros: BGM_AMB_CHIKA, Slime Morimori Dragon Quest 2: SE_187, SE_210, Advance Wars)
      case 0xA0: {
        u8 subStatusByte;
        s16 randMin;
        s16 randMax;

        subStatusByte = readByte(curOffset++);
        randMin = (signed) readShort(curOffset);
        curOffset += 2;
        randMax = (signed) readShort(curOffset);
        curOffset += 2;

        addUnknown(beginOffset, curOffset - beginOffset, "Cmd with Random Value");
        break;
      }

      // [loveemu] (ex: New Mario Bros: BGM_AMB_SABAKU)
      case 0xA1: {
        u8 subStatusByte = readByte(curOffset++);
        u8 varNumber = readByte(curOffset++);

        addUnknown(beginOffset, curOffset - beginOffset, "Cmd with Variable");
        break;
      }

      case 0xA2: {
        addUnknown(beginOffset, curOffset - beginOffset, "If");
        break;
      }

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
      case 0xBD: {
        u8 varNumber;
        s16 val;
        const char* eventName[] = {
            "Set Variable", "Add Variable", "Sub Variable", "Mul Variable", "Div Variable",
            "Shift Vabiable", "Rand Variable", "", "If Variable ==", "If Variable >=",
            "If Variable >", "If Variable <=", "If Variable <", "If Variable !="
        };

        varNumber = readByte(curOffset++);
        val = readShort(curOffset);
        curOffset += 2;

        addUnknown(beginOffset, curOffset - beginOffset, eventName[status_byte - 0xB0]);
        break;
      }

      case 0xC0: {
        u8 pan = readByte(curOffset++);
        addPan(beginOffset, curOffset - beginOffset, pan);
        break;
      }

      case 0xC1:
        vol = readByte(curOffset++);
        addVol(beginOffset, curOffset - beginOffset, vol);
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_BOSS1_)
      case 0xC2: {
        u8 mvol = readByte(curOffset++);
        addUnknown(beginOffset, curOffset - beginOffset, "Master Volume");
        break;
      }

      // [loveemu] (ex: Puyo Pop Fever 2: BGM00)
      case 0xC3: {
        s8 transpose = (signed) readByte(curOffset++);
        addTranspose(beginOffset, curOffset - beginOffset, transpose);
//			AddGenericEvent(beginOffset, curOffset-beginOffset, "Transpose", NULL, BG_CLR_GREEN);
        break;
      }

      // [loveemu] pitch bend (ex: Castlevania Dawn of Sorrow: BGM_BOSS2)
      case 0xC4: {
        s16 bend = (signed) readByte(curOffset++) * 64;
        addPitchBend(beginOffset, curOffset - beginOffset, bend);
        break;
      }

      // [loveemu] pitch bend range (ex: Castlevania Dawn of Sorrow: BGM_BOSS2)
      case 0xC5: {
        u8 semitones = readByte(curOffset++);
        addPitchBendRange(beginOffset, curOffset - beginOffset, semitones * 100);
        break;
      }

      // [loveemu] (ex: Children of Mana: SEQ_BGM000)
      case 0xC6:
        curOffset++;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Priority", "", Type::ChangeState);
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
      case 0xC7: {
        u8 notewait = readByte(curOffset++);
        noteWithDelta = (notewait != 0);
        addUnknown(beginOffset, curOffset - beginOffset, "Notewait Mode");
        break;
      }

      // [loveemu] (ex: Hanjuku Hero DS: NSE_42)
      case 0xC8:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Tie");
        break;

      // [loveemu] (ex: Hanjuku Hero DS: NSE_50)
      case 0xC9:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Portamento Control");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
      case 0xCA: {
        u8 amount = readByte(curOffset++);
        addModulation(beginOffset, curOffset - beginOffset, amount, "Modulation Depth");
        break;
      }

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
      case 0xCB:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Modulation Speed");
        break;

      // [loveemu] (ex: Children of Mana: SEQ_BGM001)
      case 0xCC:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Modulation Type");
        break;

      // [loveemu] (ex: Phoenix Wright - Ace Attorney: BGM021)
      case 0xCD:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Modulation Range");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
      case 0xCE: {
        bool bPortOn = (readByte(curOffset++) != 0);
        addPortamento(beginOffset, curOffset - beginOffset, bPortOn);
        break;
      }

      // [loveemu] (ex: Bomberman: SEQ_AREA04)
      case 0xCF: {
        u8 portTime = readByte(curOffset++);
        addPortamentoTime(beginOffset, curOffset - beginOffset, portTime);
        break;
      }

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xD0:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Attack Rate");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xD1:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Decay Rate");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xD2:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Sustain Level");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xD3:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Release Rate");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xD4:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Loop Start");
        break;

      case 0xD5: {
        u8 expression = readByte(curOffset++);
        addExpression(beginOffset, curOffset - beginOffset, expression);
        break;
      }

      // [loveemu]
      case 0xD6:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Print Variable");
        break;

      // [loveemu] (ex: Children of Mana: SEQ_BGM001)
      case 0xE0:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset, "Modulation Delay");
        break;

      case 0xE1: {
        u16 bpm = readShort(curOffset);
        curOffset += 2;
        addTempoBPM(beginOffset, curOffset - beginOffset, bpm);
        break;
      }

      // [loveemu] (ex: Hippatte! Puzzle Bobble: SEQ_1pbgm03)
      case 0xE3:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset, "Sweep Pitch");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xFC:
        addUnknown(beginOffset, curOffset - beginOffset, "Loop End");
        break;

      case 0xFD: {
        return addReturn(beginOffset, curOffset - beginOffset, "Return");
      }

      // [loveemu] allocate track, however should not handle in this function
      case 0xFE:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset, "Allocate Track");
        break;

      case 0xFF:
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;

      default:
        addUnknown(beginOffset, curOffset - beginOffset);
        return false;
    }
  return true;
}
