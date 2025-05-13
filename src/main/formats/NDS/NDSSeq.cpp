#include "NDSSeq.h"

DECLARE_FORMAT(NDS);

using namespace std;

NDSSeq::NDSSeq(RawFile *file, uint32_t offset, uint32_t length, string name)
    : VGMSeq(NDSFormat::name, file, offset, length, name) {
}

bool NDSSeq::parseHeader(void) {
  VGMHeader *SSEQHdr = addHeader(dwOffset, 0x10, "SSEQ Chunk Header");
  SSEQHdr->addSig(dwOffset, 8);
  SSEQHdr->addChild(dwOffset + 8, 4, "Size");
  SSEQHdr->addChild(dwOffset + 12, 2, "Header Size");
  SSEQHdr->addUnknownChild(dwOffset + 14, 2);
  //SeqChunkHdr->addSimpleChild(dwOffset, 4, "Blah");
  unLength = readWord(dwOffset + 8);
  setPPQN(0x30);
  return true;        //successful
}

bool NDSSeq::parseTrackPointers(void) {
  VGMHeader *DATAHdr = addHeader(dwOffset + 0x10, 0xC, "DATA Chunk Header");
  DATAHdr->addSig(dwOffset + 0x10, 4);
  DATAHdr->addChild(dwOffset + 0x10 + 4, 4, "Size");
  DATAHdr->addChild(dwOffset + 0x10 + 8, 4, "Data Pointer");
  uint32_t offset = dwOffset + 0x1C;
  uint8_t b = readByte(offset);
  aTracks.push_back(new NDSTrack(this));

  //FE XX XX signifies multiple tracks, each true bit in the XX values signifies there is a track for that channel
  if (b == 0xFE)
  {
    VGMHeader *TrkPtrs = addHeader(offset, 0, "Track Pointers");
    TrkPtrs->addChild(offset, 3, "Valid Tracks");
    offset += 3;    //but all we need to do is check for subsequent 0x93 track pointer events
    b = readByte(offset);
    uint32_t songDelay = 0;

    while (b == 0x80) {
      uint32_t value;
      uint8_t c;
      uint32_t beginOffset = offset;
      offset++;
      if ((value = readByte(offset++)) & 0x80) {
        value &= 0x7F;
        do {
          value = (value << 7) + ((c = readByte(offset++)) & 0x7F);
        } while (c & 0x80);
      }
      songDelay += value;
      TrkPtrs->addChild(beginOffset, offset - beginOffset, "Delay");
      //songDelay += SeqTrack::ReadVarLen(++offset);
      b = readByte(offset);
      break;
    }

    //Track/Channel assignment and pointer.  Channel # is irrelevant
    while (b == 0x93)
    {
      TrkPtrs->addChild(offset, 5, "Track Pointer");
      uint32_t trkOffset = readByte(offset + 2) + (readByte(offset + 3) << 8) +
          (readByte(offset + 4) << 16) + dwOffset + 0x1C;
      NDSTrack *newTrack = new NDSTrack(this, trkOffset);
      aTracks.push_back(newTrack);
      //newTrack->
      offset += 5;
      b = readByte(offset);
    }
    TrkPtrs->unLength = offset - TrkPtrs->dwOffset;
  }
  aTracks[0]->dwOffset = offset;
  aTracks[0]->dwStartOffset = offset;
  return true;
}

//  ********
//  NDSTrack
//  ********

NDSTrack::NDSTrack(NDSSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
}

void NDSTrack::resetVars() {
  jumpCount = 0;
  loopReturnOffset = 0;
  hasLoopReturnOffset = false;
  SeqTrack::resetVars();
}

bool NDSTrack::readEvent(void) {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);

  if (status_byte < 0x80) //then it's a note on event
  {
    uint8_t vel = readByte(curOffset++);
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
        uint8_t newProg = (uint8_t) readVarLen(curOffset);
        addProgramChange(beginOffset, curOffset - beginOffset, newProg);
        break;
      }

      // [loveemu] open track, however should not handle in this function
      case 0x93:
        curOffset += 4;
        addUnknown(beginOffset, curOffset - beginOffset, "Open Track");
        break;

      case 0x94: {
        uint32_t jumpAddr = readByte(curOffset) + (readByte(curOffset + 1) << 8)
            + (readByte(curOffset + 2) << 16) + parentSeq->dwOffset + 0x1C;
        curOffset += 3;

        // Add an End Track if it exists afterward, for completeness sake
        if (readMode == READMODE_ADD_TO_UI && !isOffsetUsed(curOffset)) {
          if (readByte(curOffset) == 0xFF) {
            addGenericEvent(curOffset, 1, "End of Track", "", Type::TrackEnd, ICON_TRACKEND);
          }
        }

        // The event usually appears at last of the song, but there can be an exception.
        // See Zelda The Spirit Tracks - SSEQ_0018 (overworld train theme)
        bool bContinue = true;
        if (isOffsetUsed(jumpAddr)) {
          addLoopForever(beginOffset, 4, "Loop");
          bContinue = false;
        }
        else {
          addGenericEvent(beginOffset, 4, "Jump", "", Type::LoopForever);
        }

        curOffset = jumpAddr;
        return bContinue;
      }

      case 0x95:
        hasLoopReturnOffset = true;
        loopReturnOffset = curOffset + 3;
        addGenericEvent(beginOffset, curOffset + 3 - beginOffset, "Call", "", Type::Loop);
        curOffset = readByte(curOffset) + (readByte(curOffset + 1) << 8)
            + (readByte(curOffset + 2) << 16) + parentSeq->dwOffset + 0x1C;
        break;

      // [loveemu] (ex: Hanjuku Hero DS: NSE_45, New Mario Bros: BGM_AMB_CHIKA, Slime Morimori Dragon Quest 2: SE_187, SE_210, Advance Wars)
      case 0xA0: {
        uint8_t subStatusByte;
        int16_t randMin;
        int16_t randMax;

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
        uint8_t subStatusByte = readByte(curOffset++);
        uint8_t varNumber = readByte(curOffset++);

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
        uint8_t varNumber;
        int16_t val;
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
        uint8_t pan = readByte(curOffset++);
        addPan(beginOffset, curOffset - beginOffset, pan);
        break;
      }

      case 0xC1:
        vol = readByte(curOffset++);
        addVol(beginOffset, curOffset - beginOffset, vol);
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_BOSS1_)
      case 0xC2: {
        uint8_t mvol = readByte(curOffset++);
        addUnknown(beginOffset, curOffset - beginOffset, "Master Volume");
        break;
      }

      // [loveemu] (ex: Puyo Pop Fever 2: BGM00)
      case 0xC3: {
        int8_t transpose = (signed) readByte(curOffset++);
        addTranspose(beginOffset, curOffset - beginOffset, transpose);
//			AddGenericEvent(beginOffset, curOffset-beginOffset, "Transpose", NULL, BG_CLR_GREEN);
        break;
      }

      // [loveemu] pitch bend (ex: Castlevania Dawn of Sorrow: BGM_BOSS2)
      case 0xC4: {
        int16_t bend = (signed) readByte(curOffset++) * 64;
        addPitchBend(beginOffset, curOffset - beginOffset, bend);
        break;
      }

      // [loveemu] pitch bend range (ex: Castlevania Dawn of Sorrow: BGM_BOSS2)
      case 0xC5: {
        uint8_t semitones = readByte(curOffset++);
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
        uint8_t notewait = readByte(curOffset++);
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
        uint8_t amount = readByte(curOffset++);
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
        uint8_t portTime = readByte(curOffset++);
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
        uint8_t expression = readByte(curOffset++);
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
        uint16_t bpm = readShort(curOffset);
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
        // This event usually does not cause an infinite loop.
        // However, a complicated sequence with a ton of conditional events, it sometimes confuses the parser and causes an infinite loop.
        // See Animal Crossing: Wild World - SSEQ_270
        bool bContinue = true;
        if (!hasLoopReturnOffset || isOffsetUsed(loopReturnOffset)) {
          bContinue = false;
        }

        addGenericEvent(beginOffset, curOffset - beginOffset, "Return", "", Type::Loop);
        curOffset = loopReturnOffset;
        return bContinue;
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
