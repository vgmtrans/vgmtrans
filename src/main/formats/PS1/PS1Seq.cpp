/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PS1Seq.h"
#include "formats/PS1/PS1Format.h"

DECLARE_FORMAT(PS1)

PS1Seq::PS1Seq(RawFile *file, uint32_t offset) : VGMSeqNoTrks(PS1Format::name, file, offset, "PS1 Seq") {
  useReverb();
  //bWriteInitialTempo = false; // false, because the initial tempo is added by tempo event
}

PS1Seq::~PS1Seq() {
}


bool PS1Seq::parseHeader() {
  setPPQN(readShortBE(offset() + 8));
  nNumTracks = 16;

  uint8_t numer = readByte(offset() + 0x0D);
  uint8_t denom = readByte(offset() + 0x0E);
  if (numer == 0 || numer > 32)                //sanity check
    return false;

  VGMHeader *seqHeader = VGMSeq::addHeader(offset(), 11, "Sequence Header");
  seqHeader->addChild(offset(), 4, "ID");
  seqHeader->addChild(offset() + 0x04, 4, "Version");
  seqHeader->addChild(offset() + 0x08, 2, "Resolution of quarter note");
  seqHeader->addTempo(offset() + 0x0A, 3);
  seqHeader->addSig(offset() + 0x0D, 2); // Rhythm (Numerator) and Rhythm (Denominator) (2^n)

  if (readByte(offset() + 0xF) == 0 && readByte(offset() + 0x10) == 0) {
    setEventsOffset(offset() + 0x0F + 4);
    PS1Seq *newPS1Seq = new PS1Seq(rawFile(), offset() + readShortBE(offset() + 0x11) + 0x13 - 6);
    if (!newPS1Seq->loadVGMFile()) {
      delete newPS1Seq;
    }
    //short relOffset = (short)GetShortBE(curOffset);
    //AddGenericEvent(beginOffset, 4, "Jump Relative", NULL, BG_CLR_PINK);
    //curOffset += relOffset;
  }
  else {
    setEventsOffset(offset() + 0x0F);
  }

  return true;
}

void PS1Seq::resetVars() {
  VGMSeqNoTrks::resetVars();

  uint32_t initialTempo = (readShortBE(offset() + 10) << 8) | readByte(offset() + 12);
  addTempo(offset() + 10, 3, initialTempo);

  uint8_t numer = readByte(offset() + 0x0D);
  uint8_t denom = readByte(offset() + 0x0E);
  addTimeSig(offset() + 0x0D, 2, numer, 1 << denom, (uint8_t) ppqn());
}

bool PS1Seq::readEvent() {
  uint32_t beginOffset = curOffset;
  uint32_t delta = readVarLen(curOffset);
  if (curOffset >= rawFile()->size())
    return false;
  addTime(delta);

  uint8_t status_byte = readByte(curOffset++);

  //if (status_byte == 0)				//Jump Relative
  //{
  //	short relOffset = (short)GetShortBE(curOffset);
  //	AddGenericEvent(beginOffset, 4, "Jump Relative", NULL, BG_CLR_PINK);
  //	curOffset += relOffset;

  //	curOffset += 4;		//skip the first 4 bytes (no idea)
  //	SetPPQN(GetShortBE(curOffset));
  //	curOffset += 2;
  //	AddTempo(curOffset, 3, GetWordBE(curOffset-1) & 0xFFFFFF);
  //	curOffset += 3;
  //	uint8_t numer = GetByte(curOffset++);
  //	uint8_t denom = GetByte(curOffset++);
  //	if (numer == 0 || numer > 32)				//sanity check
  //		return false;
  //	AddTimeSig(curOffset-2, 2, numer, 1<<denom, GetPPQN());
  //	SetEventsOffset(offset() + 0x0F);
  //}
//	else
  // Running Status
  if (status_byte <= 0x7F) {
    if (status_byte == 0) {
      // Workaround: some games (exactly what?) were ripped to PSF with the EndTrack event missing,
      // so if we read a sequence of 0 bytes, then just treat that as the end of the track.
      //
      // EXCEPTIONAL CASE: Mega Man 8: 12. Ruins Stage.psf (Sword Man stage)
      // 3182D: 00 C0 00 00 00 00 00 00 00 00 00 00 C2 02

      constexpr size_t kZeroDataLengthThreshold = 10;
      bool no_next_data = true;
      for (off_t off = beginOffset; off < beginOffset + kZeroDataLengthThreshold; off++) {
        if (readByte(off) != 0) {
          no_next_data = false;
          break;
        }
      }

      if (no_next_data) {
        L_WARN("SEQ parser has reached zero-filled data at 0x{:X}. Parser terminates the analysis"
          "because it is considered to be out of bounds of the song data.", beginOffset);
        return false;
      }
    }
    status_byte = runningStatus;
    curOffset--;
  }
  else
    runningStatus = status_byte;


  channel = status_byte & 0x0F;
  setCurTrack(channel);

  switch (status_byte & 0xF0) {
    //note event
    case 0x90 : {
      auto key = readByte(curOffset++);
      auto vel = readByte(curOffset++);
      //if the velocity is > 0, it's a note on. otherwise it's a note off
      if (vel > 0)
        addNoteOn(beginOffset, curOffset - beginOffset, key, vel);
      else
        addNoteOff(beginOffset, curOffset - beginOffset, key);
      break;
    }

    case 0xB0 : {
      uint8_t controlNum = readByte(curOffset++);
      uint8_t value = readByte(curOffset++);
      switch (controlNum)
      {
        //bank select
        case 0 :
          addGenericEvent(beginOffset, curOffset - beginOffset, "Bank Select", "", CLR_MISC);
          addBankSelectNoItem(value);
          break;

        //data entry
        case 6 :
          addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN Data Entry", "", CLR_MISC);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        //volume
        case 7 :
          addVol(beginOffset, curOffset - beginOffset, value);
          break;

        //pan
        case 10 :
          addPan(beginOffset, curOffset - beginOffset, value);
          break;

        //expression
        case 11 :
          addExpression(beginOffset, curOffset - beginOffset, value);
          break;

        //damper (hold)
        case 64 :
          addSustainEvent(beginOffset, curOffset - beginOffset, value);
          break;

        //reverb depth (_SsContExternal)
        case 91 :
          addReverb(beginOffset, curOffset - beginOffset, value);
          break;

        //(0x62) NRPN 1 (LSB)
        case 98 :
          switch (value) {
            case 20 :
              addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 1 #20", "", CLR_MISC);
              break;

            case 30 :
              addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 1 #30", "", CLR_MISC);
              break;

            default:
              addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 1", "", CLR_MISC);
              break;
          }

          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        //(0x63) NRPN 2 (MSB)
        case 99 :
          switch (value) {
            case 20 :
              addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", "", CLR_LOOP);
              break;

            case 30 :
              addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", "", CLR_LOOP);
              break;

            default:
              addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 2", "", CLR_MISC);
              break;
          }

          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        //(0x64) RPN 1 (LSB), no effect?
        case 100 :
          addGenericEvent(beginOffset, curOffset - beginOffset, "RPN 1", "", CLR_MISC);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        //(0x65) RPN 2 (MSB), no effect?
        case 101 :
          addGenericEvent(beginOffset, curOffset - beginOffset, "RPN 2", "", CLR_MISC);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        //reset all controllers
        case 121 :
          addGenericEvent(beginOffset, curOffset - beginOffset, "Reset All Controllers", "", CLR_MISC);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        default:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control Event", "", CLR_UNKNOWN);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;
      }
    }
      break;

    case 0xC0 : {
      uint8_t progNum = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, progNum);
    }
      break;

    case 0xE0 : {
      uint8_t hi = readByte(curOffset++);
      uint8_t lo = readByte(curOffset++);
      addPitchBendMidiFormat(beginOffset, curOffset - beginOffset, hi, lo);
    }
      break;

    case 0xF0 : {
      if (status_byte == 0xFF) {
        switch (readByte(curOffset++)) {
          //tempo.  This is different from SMF, where we'd expect a 51 then 03.  Also, supports a string of tempo events
          case 0x51 :
            addTempo(beginOffset, curOffset + 3 - beginOffset, (readShortBE(curOffset) << 8) | readByte(curOffset + 2));
            curOffset += 3;
            break;

          case 0x2F :
            addEndOfTrack(beginOffset, curOffset - beginOffset);
            return false;

          default :
            addUnknown(beginOffset, curOffset - beginOffset, "Meta Event");
            return false;
        }
      }
      else {
        addUnknown(beginOffset, curOffset - beginOffset);
        return false;
      }
    }
      break;

    default:
      addUnknown(beginOffset, curOffset - beginOffset);
      return false;
  }
  return true;
}
