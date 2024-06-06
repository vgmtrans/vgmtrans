/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PS1Seq.h"
#include "formats/PS1/PS1Format.h"

DECLARE_FORMAT(PS1)

PS1Seq::PS1Seq(RawFile *file, uint32_t offset) : VGMSeqNoTrks(PS1Format::name, file, offset, "PS1 Seq") {
  UseReverb();
  //bWriteInitialTempo = false; // false, because the initial tempo is added by tempo event
}

PS1Seq::~PS1Seq() {
}


bool PS1Seq::GetHeaderInfo() {
  SetPPQN(GetShortBE(offset() + 8));
  nNumTracks = 16;

  uint8_t numer = GetByte(offset() + 0x0D);
  uint8_t denom = GetByte(offset() + 0x0E);
  if (numer == 0 || numer > 32)                //sanity check
    return false;

  VGMHeader *seqHeader = VGMSeq::addHeader(offset(), 11, "Sequence Header");
  seqHeader->addChild(offset(), 4, "ID");
  seqHeader->addChild(offset() + 0x04, 4, "Version");
  seqHeader->addChild(offset() + 0x08, 2, "Resolution of quarter note");
  seqHeader->AddTempo(offset() + 0x0A, 3);
  seqHeader->AddSig(offset() + 0x0D, 2); // Rhythm (Numerator) and Rhythm (Denominator) (2^n)

  if (GetByte(offset() + 0xF) == 0 && GetByte(offset() + 0x10) == 0) {
    SetEventsOffset(offset() + 0x0F + 4);
    PS1Seq *newPS1Seq = new PS1Seq(rawFile(), offset() + GetShortBE(offset() + 0x11) + 0x13 - 6);
    if (!newPS1Seq->LoadVGMFile()) {
      delete newPS1Seq;
    }
    //short relOffset = (short)GetShortBE(curOffset);
    //AddGenericEvent(beginOffset, 4, "Jump Relative", NULL, BG_CLR_PINK);
    //curOffset += relOffset;
  }
  else {
    SetEventsOffset(offset() + 0x0F);
  }

  return true;
}

void PS1Seq::ResetVars() {
  VGMSeqNoTrks::ResetVars();

  uint32_t initialTempo = (GetShortBE(offset() + 10) << 8) | GetByte(offset() + 12);
  AddTempo(offset() + 10, 3, initialTempo);

  uint8_t numer = GetByte(offset() + 0x0D);
  uint8_t denom = GetByte(offset() + 0x0E);
  AddTimeSig(offset() + 0x0D, 2, numer, 1 << denom, (uint8_t) GetPPQN());
}

bool PS1Seq::ReadEvent() {
  uint32_t beginOffset = curOffset;
  uint32_t delta = ReadVarLen(curOffset);
  if (curOffset >= rawFile()->size())
    return false;
  AddTime(delta);

  uint8_t status_byte = GetByte(curOffset++);

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
        if (GetByte(off) != 0) {
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
  SetCurTrack(channel);

  switch (status_byte & 0xF0) {
    //note event
    case 0x90 : {
      auto key = GetByte(curOffset++);
      auto vel = GetByte(curOffset++);
      //if the velocity is > 0, it's a note on. otherwise it's a note off
      if (vel > 0)
        AddNoteOn(beginOffset, curOffset - beginOffset, key, vel);
      else
        AddNoteOff(beginOffset, curOffset - beginOffset, key);
      break;
    }

    case 0xB0 : {
      uint8_t controlNum = GetByte(curOffset++);
      uint8_t value = GetByte(curOffset++);
      switch (controlNum)
      {
        //bank select
        case 0 :
          AddGenericEvent(beginOffset, curOffset - beginOffset, "Bank Select", "", CLR_MISC);
          AddBankSelectNoItem(value);
          break;

        //data entry
        case 6 :
          AddGenericEvent(beginOffset, curOffset - beginOffset, "NRPN Data Entry", "", CLR_MISC);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->AddControllerEvent(channel, controlNum, value);
          }
          break;

        //volume
        case 7 :
          AddVol(beginOffset, curOffset - beginOffset, value);
          break;

        //pan
        case 10 :
          AddPan(beginOffset, curOffset - beginOffset, value);
          break;

        //expression
        case 11 :
          AddExpression(beginOffset, curOffset - beginOffset, value);
          break;

        //damper (hold)
        case 64 :
          AddSustainEvent(beginOffset, curOffset - beginOffset, value);
          break;

        //reverb depth (_SsContExternal)
        case 91 :
          AddReverb(beginOffset, curOffset - beginOffset, value);
          break;

        //(0x62) NRPN 1 (LSB)
        case 98 :
          switch (value) {
            case 20 :
              AddGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 1 #20", "", CLR_MISC);
              break;

            case 30 :
              AddGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 1 #30", "", CLR_MISC);
              break;

            default:
              AddGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 1", "", CLR_MISC);
              break;
          }

          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->AddControllerEvent(channel, controlNum, value);
          }
          break;

        //(0x63) NRPN 2 (MSB)
        case 99 :
          switch (value) {
            case 20 :
              AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", "", CLR_LOOP);
              break;

            case 30 :
              AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", "", CLR_LOOP);
              break;

            default:
              AddGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 2", "", CLR_MISC);
              break;
          }

          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->AddControllerEvent(channel, controlNum, value);
          }
          break;

        //(0x64) RPN 1 (LSB), no effect?
        case 100 :
          AddGenericEvent(beginOffset, curOffset - beginOffset, "RPN 1", "", CLR_MISC);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->AddControllerEvent(channel, controlNum, value);
          }
          break;

        //(0x65) RPN 2 (MSB), no effect?
        case 101 :
          AddGenericEvent(beginOffset, curOffset - beginOffset, "RPN 2", "", CLR_MISC);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->AddControllerEvent(channel, controlNum, value);
          }
          break;

        //reset all controllers
        case 121 :
          AddGenericEvent(beginOffset, curOffset - beginOffset, "Reset All Controllers", "", CLR_MISC);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->AddControllerEvent(channel, controlNum, value);
          }
          break;

        default:
          AddGenericEvent(beginOffset, curOffset - beginOffset, "Control Event", "", CLR_UNKNOWN);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->AddControllerEvent(channel, controlNum, value);
          }
          break;
      }
    }
      break;

    case 0xC0 : {
      uint8_t progNum = GetByte(curOffset++);
      AddProgramChange(beginOffset, curOffset - beginOffset, progNum);
    }
      break;

    case 0xE0 : {
      uint8_t hi = GetByte(curOffset++);
      uint8_t lo = GetByte(curOffset++);
      AddPitchBendMidiFormat(beginOffset, curOffset - beginOffset, hi, lo);
    }
      break;

    case 0xF0 : {
      if (status_byte == 0xFF) {
        switch (GetByte(curOffset++)) {
          //tempo.  This is different from SMF, where we'd expect a 51 then 03.  Also, supports a string of tempo events
          case 0x51 :
            AddTempo(beginOffset, curOffset + 3 - beginOffset, (GetShortBE(curOffset) << 8) | GetByte(curOffset + 2));
            curOffset += 3;
            break;

          case 0x2F :
            AddEndOfTrack(beginOffset, curOffset - beginOffset);
            return false;

          default :
            AddUnknown(beginOffset, curOffset - beginOffset, "Meta Event");
            return false;
        }
      }
      else {
        AddUnknown(beginOffset, curOffset - beginOffset);
        return false;
      }
    }
      break;

    default:
      AddUnknown(beginOffset, curOffset - beginOffset);
      return false;
  }
  return true;
}
