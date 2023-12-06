#include "pch.h"
#include "CPSTrackV1.h"
#include "ScaleConversion.h"
#include "SeqEvent.h"

using namespace std;

// *************
// CPSTrackV1
// *************


CPSTrackV1::CPSTrackV1(CPSSeq *parentSeq, long offset, long length)
    : SeqTrack(parentSeq, offset, length) {
  ResetVars();
}

void CPSTrackV1::ResetVars() {
  dur = 0xFF;        // verified in progear (required there as well)
  bPrevNoteTie = 0;
  prevTieNote = 0;
  curDeltaTable = 0;
  noteState = 0;
  bank = 0;
  memset(loop, 0, sizeof(loop));
  memset(loopOffset, 0, sizeof(loopOffset));
  SeqTrack::ResetVars();
}

void CPSTrackV1::AddInitialMidiEvents(int trackNum) {
  SeqTrack::AddInitialMidiEvents(trackNum);
  AddPortamentoTime14BitNoItem(0);
}


bool CPSTrackV1::ReadEvent(void) {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = GetByte(curOffset++);

  uint32_t delta;

  if (status_byte >= 0x20) {

    if ((noteState & 0x30) == 0)
      curDeltaTable = 1;
    else if ((noteState & 0x10) == 0)
      curDeltaTable = 0;
    else {
      noteState &= ~(1 << 4);        //RES 4		at 0xBD6 in sfa2
      curDeltaTable = 2;
    }

    // effectively, use the highest 3 bits of the status byte as index to delta_table.
    // this code starts at 0xBB3 in sfa2
    delta = delta_table[curDeltaTable][((status_byte >> 5) & 7) - 1];
    delta <<= 4;

    //if it's not a rest
    if ((status_byte & 0x1F) != 0) {
      // for 100% accuracy, we'd be shifting by 8, but that seems excessive for MIDI
      uint32_t absDur = (uint32_t) ((double) (delta / (double) (256 << 4)) * (double) (dur << 4));

      key = (status_byte & 0x1F) + octave_table[noteState & 0x0F] - 1;

      // Tie note
      if ((noteState & 0x40) > 0) {
        if (!bPrevNoteTie) {
          AddNoteOn(beginOffset, curOffset - beginOffset, key, 127, L"Note On (tied / with portamento)");
          AddPortamentoNoItem(true);
        }
        else if (key != prevTieNote) {
          AddNoteOn(beginOffset, curOffset - beginOffset, key, 127, L"Note On (tied)");
          AddNoteOffNoItem(prevTieNote);
        }
        else
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", L"", CLR_NOTEON);
        bPrevNoteTie = true;
        prevTieNote = key;
      }
      else {
        if (bPrevNoteTie) {
          if (key != prevTieNote) {
            AddNoteOffNoItem(prevTieNote);
            AddNoteByDur(beginOffset, curOffset - beginOffset, key, 127, absDur);
            InsertPortamentoNoItem(false, GetTime()+absDur);
          }
          else {
            AddTime(absDur);
            delta -= absDur;
            AddNoteOff(beginOffset, curOffset - beginOffset, prevTieNote, L"Note Off (tied)");
            AddPortamentoNoItem(false);
          }
        }
        else {
          AddNoteByDur(beginOffset, curOffset - beginOffset, key, 127, absDur);
        }
        bPrevNoteTie = false;
      }
    }
    else {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Rest", L"", CLR_REST);
    }
    AddTime(delta);
  }
  else {
    int loopNum;
    switch (status_byte) {
      case 0x00 :
        noteState ^= 0x20;
        AddGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        L"Note State xor 0x20 (change duration table)",
                        L"",
                        CLR_CHANGESTATE);
        break;
      case 0x01 :
        noteState ^= 0x40;
        AddGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        L"Note State xor 0x40 (Toggle tie)",
                        L"",
                        CLR_CHANGESTATE);
        break;
      case 0x02 :
        noteState |= (1 << 4);
        AddGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        L"Note State |= 0x10 (change duration table)",
                        L"",
                        CLR_CHANGESTATE);
        break;
      case 0x03 :
        noteState ^= 8;
        AddGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        L"Note State xor 8 (change octave)",
                        L"",
                        CLR_CHANGESTATE);
        break;

      case 0x04 :
        noteState &= 0x97;
        noteState |= GetByte(curOffset++);
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Change Note State (& 0x97)", L"", CLR_CHANGESTATE);
        break;

      case 0x05 : {
        CPSFormatVer fmt_version = ((CPSSeq *) parentSeq)->fmt_version;
        //if (isEqual(fmt_version, 1.71))

        if (((CPSSeq *) parentSeq)->fmt_version >= VER_140) {
          // This byte is clearly the desired BPM, however there is a loss of resolution when the driver
          // converts this value because it is represented with 16 bits... See the table in sfa3 at 0x3492.
          // I've decided to keep the desired BPM rather than use the exact tempo value from the table
          uint8_t tempo = GetByte(curOffset++);
          AddTempoBPM(beginOffset, curOffset - beginOffset, tempo);
        }
        else {
          // In versions < 1.40, the tempo value is subtracted from the remaining delta duration every tick.
          // We know the ppqn is 48 (0x30) because this results in clean looking MIDIs.  Internally,
          // 0x30 is left-shifted by 8 to get 0x3000 for finer resolution.
          // We also know that a tick is run 62.75 times a second, because the Z80 interrupt timer is 250 Hz, but code
          // at 0xF9 in sfa2 shows it runs once every 4 ticks (250/4 = 62.5).  There are 0.5 seconds in a beat at 120bpm.
          // So, to get the tempo value that would equal 120bpm...
          // 62.75*0.5*x = 12288
          //   x = 393.216
          // We divide this by 120 to get the value we divide by to convert to BPM, or put another way:
          // (0x3000 / (62.5*0.5)) / x = 120
          //   x = 3.2768
          // So we must divide the provided tempo value by 3.2768 to get the actual BPM.  Btw, there is a table in
          // sfa3 at 0x3492 which converts a BPM index to the x value in the first equation.  It seems to confirm our math
          uint16_t tempo = GetShortBE(curOffset);
          double fTempo = tempo / 3.2768;
          curOffset += 2;
          AddTempoBPM(beginOffset, curOffset - beginOffset, fTempo);
          //RecordMidiSetTempoBPM(current_delta_time, flValue1, hFile);
        }
        break;
      }

      case 0x06 :
        dur = GetByte(curOffset++);
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Set Duration", L"", CLR_CHANGESTATE);
        break;

      case 0x07 :
        vol = GetByte(curOffset++);
        vol = ConvertPercentAmpToStdMidiVal(vol_table[vol] / (double) 0x1FFF);
        this->AddVol(beginOffset, curOffset - beginOffset, vol);
        break;

      case 0x08 : {
        uint8_t progNum = GetByte(curOffset++);
        //if (((CPSSeq*)parentSeq)->fmt_version < VER_116) {
        AddBankSelectNoItem((bank * 2) + (progNum / 128));
        AddProgramChange(beginOffset, curOffset - beginOffset, progNum % 128);
        //}
        //else
        //	AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
        break;
      }

      //effectively sets the octave
      case 0x09 :
        noteState &= 0xF8;
        noteState |= GetByte(curOffset++);
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Set Octave", L"", CLR_CHANGESTATE);
        break;

      // Global Transpose
      case 0x0A : {
        int8_t globTranspose = GetByte(curOffset++);
        AddGlobalTranspose(beginOffset, curOffset - beginOffset, globTranspose);
        break;
      }

      //Transpose
      case 0x0B :
        transpose = GetByte(curOffset++);
        AddTranspose(beginOffset, curOffset - beginOffset, transpose);
        break;

      //Pitch bend - only gets applied at Note-on, but don't care.  It's used mostly as a detune for chorus effect
      // pitch bend value is a signed byte with a range of +/- 50 cents.
      case 0x0C : {
        uint8_t pitchbend = GetByte(curOffset++);
        //double cents = (pitchbend / 256.0) * 100;
        AddMarker(beginOffset,
                  curOffset - beginOffset,
                  string("pitchbend"),
                  pitchbend,
                  0,
                  L"Pitch Bend",
                  PRIORITY_MIDDLE,
                  CLR_PITCHBEND);
        //AddPitchBend(beginOffset, curOffset-beginOffset, (cents / 200) * 8192);
      }
      break;
      case 0x0D : {
        // Portamento: take the rate value, left shift it 1.  This value * (100/256) is increment in cents every 1/(250/4) seconds until we hit target key.
        // A portamento rate value of 0 means instantaneous slide
        uint8_t portamentoRate = GetByte(curOffset++);
        uint16_t value = 0;
        // the rate is represented as decacents per second, but is passed in on an inverse 14 bit scale (from 0-0x3FFF)
        // value of 0 means a rate of 0x3FFF decacents per second - the fastest slide
        // value of 0x3FFF means 0 decacents per second - no slide
        if (portamentoRate != 0) {
          auto centsPerSecond = static_cast<uint16_t>(static_cast<double>(portamentoRate) * 2 * (100.0/256.0) * (256.0/4.0));
          auto decacentsPerSecond = std::min(static_cast<uint16_t>(centsPerSecond / 10), static_cast<uint16_t>(0x3FFF));
          value = 0x3FFF - decacentsPerSecond;
        }
        AddPortamentoTime14Bit(beginOffset, curOffset - beginOffset, value);
        break;
      }

      //loop
      case 0x0E :
        loopNum = 0;
        goto theLoop;

      case 0x0F :
        loopNum = 1;
        goto theLoop;

      case 0x10 :
        loopNum = 2;
        goto theLoop;

      case 0x11 :
        loopNum = 3;
      theLoop:
        if (loop[loopNum] == 0 && loopOffset[loopNum] == 0)                        //first time hitting loop
        {
          loopOffset[loopNum] = curOffset;
          loop[loopNum] = GetByte(curOffset++);    //set the number of times to loop
          bInLoop = true;
        }
        else if (loopOffset[loopNum] != curOffset) {
          // hack to check for infinite loop scenario in Punisher at 0xDF07: one 0E loop contains another 0E loop.
          // Also in slammast at f1a7, f1ab.. two 0E loops (song 0x26).  Actual game behavior is an infinite loop, see 0xF840 for
          // track ptrs repeating over the same tiny range.
          return false;

        }

        //else if (/*GetVersion() == VER_101 &&*/ loop[loopNum] > GetByte(curOffset))	// only seen in VER_101 so far (Punisher)
        //	return false;

        //already engaged in loop - decrement loop counter
        else {
          loop[loopNum]--;
          curOffset++;
        }

        uint32_t jump;
        if (((CPSSeq *) parentSeq)->fmt_version <= VER_CPS1_425) {
          jump = GetShortBE(curOffset);
          curOffset += 2;

          // sf2ce seq 0x84 seems to have a bug at D618
          // where it jumps to offset 0. verified with mame debugger.
          if (jump == 0) {
            return false;
          }
        }
        else {
          if ((GetByte(curOffset) & 0x80) == 0)
            jump = curOffset - GetByte(curOffset++);
          else {
            jump = curOffset + 2 + (int16_t)GetShortBE(curOffset);
            curOffset += 2;
          }
        }

        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop", L"", CLR_LOOP);

        if (loop[loopNum] == 0) {
          bInLoop = false;
          loopOffset[loopNum] = 0;
        }
        else {
          printf("%X JUMPING TO %X\n", curOffset, jump);
          curOffset = jump;
        }
        break;

      case 0x12 :
        loopNum = 0;
        goto loopBreak;

      case 0x13 :
        loopNum = 1;
        goto loopBreak;

      case 0x14 :
        loopNum = 2;
        goto loopBreak;

      case 0x15 :
        loopNum = 3;
      loopBreak:
        if (loop[loopNum] - 1 == 0) {
          bInLoop = false;
          loop[loopNum] = 0;
          loopOffset[loopNum] = 0;
          noteState &= 0x97;
          noteState |= GetByte(curOffset++);
          {
            uint16_t jump = GetShortBE(curOffset);
            curOffset += 2;
            AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Break", L"", CLR_LOOP);

            printf("%X LOOP BREAK JUMPING TO %X\n", curOffset, jump);
            if (((CPSSeq *) parentSeq)->fmt_version <= VER_CPS1_425) {
              curOffset = jump;
            }
            else {
              curOffset += (int16_t)jump;
            }
          }
        }
        else
          curOffset += 3;
        break;

      // Loop Always
      case 0x16 : {

        uint32_t jump;
        if (((CPSSeq *) parentSeq)->fmt_version <= VER_CPS1_425) {
          jump = GetShortBE(curOffset);
        }
        else {
          jump = curOffset + 2 + (short)GetShortBE(curOffset);
        }

        printf("%X LOOP ALWAYS JUMPING TO %X\n", curOffset, jump);
        //        curOffset += 2;
        bool bResult = AddLoopForever(beginOffset, 3);
        curOffset = jump;

        return bResult;
      }

      case 0x17 :
        AddEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;

      // pan
      case 0x18 : {
        //the pan value is b/w 0 and 0x20.  0 - hard left, 0x10 - center, 0x20 - hard right
        uint8_t pan = GetByte(curOffset++) * 4;
        if (pan != 0)
          pan--;
        this->AddPan(beginOffset, curOffset - beginOffset, pan);
        break;
      }

      case 0x19 :
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset, L"Reg9 Event (unknown to MAME)");
        break;

      case 0x1A :    //master(?) vol
      {
        //curOffset++;
        vol = GetByte(curOffset++);
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Master Volume", L"", CLR_UNKNOWN);
        //this->AddMasterVol(beginOffset, curOffset-beginOffset, vol);
        //AddVolume(beginOffset, curOffset-beginOffset, vool);
        break;
      }
        //AddUnknown(beginOffset, curOffset-beginOffset);

      //Vibrato depth...
      case 0x1B : {
        uint8_t vibratoDepth;
        if (GetVersion() < VER_171) {
          vibratoDepth = GetByte(curOffset++);
          AddMarker(beginOffset,
                    curOffset - beginOffset,
                    string("vibrato"),
                    vibratoDepth,
                    0,
                    L"Vibrato",
                    PRIORITY_HIGH,
                    CLR_PITCHBEND);
        }
        else {
          // First data byte defines behavior 0-3
          uint8_t type = GetByte(curOffset++);
          uint8_t data = GetByte(curOffset++);
          switch (type) {

            // vibrato
            case 0:
              AddMarker(beginOffset,
                        curOffset - beginOffset,
                        string("vibrato"),
                        data,
                        0,
                        L"Vibrato",
                        PRIORITY_HIGH,
                        CLR_PITCHBEND);
              break;

            // tremelo
            case 1:
              AddMarker(beginOffset,
                        curOffset - beginOffset,
                        string("tremelo"),
                        data,
                        0,
                        L"Tremelo",
                        PRIORITY_MIDDLE,
                        CLR_EXPRESSION);
              break;

            // LFO rate
            case 2:
              AddMarker(beginOffset,
                        curOffset - beginOffset,
                        string("lfo"),
                        data,
                        0,
                        L"LFO Rate",
                        PRIORITY_MIDDLE,
                        CLR_LFO);
              break;

            // LFO reset
            case 3:
              AddMarker(beginOffset,
                        curOffset - beginOffset,
                        string("resetlfo"),
                        data,
                        0,
                        L"LFO Reset",
                        PRIORITY_MIDDLE,
                        CLR_LFO);
              break;
          }
        }
        break;
      }
      case 0x1C : {
        uint8_t tremeloDepth;
        if (GetVersion() < VER_171) {
          tremeloDepth = GetByte(curOffset++);
          AddMarker(beginOffset,
                    curOffset - beginOffset,
                    string("tremelo"),
                    tremeloDepth,
                    0,
                    L"Tremelo",
                    PRIORITY_MIDDLE,
                    CLR_EXPRESSION);
        }
        else {
          // I'm not sure at all about the behavior here, need to test
          curOffset += 2;
          AddUnknown(beginOffset, curOffset - beginOffset);
        }
        break;
      }

      // LFO rate (for versions < 1.71)
      case 0x1D : {
        uint8_t rate = GetByte(curOffset++);
        if (GetVersion() < VER_171)
          AddMarker(beginOffset,
                    curOffset - beginOffset,
                    string("lfo"),
                    rate,
                    0,
                    L"LFO Rate",
                    PRIORITY_MIDDLE,
                    CLR_LFO);
        else
          AddUnknown(beginOffset, curOffset - beginOffset, L"NOP");
        break;
      }

      // Reset LFO state (for versions < 1.71)
      case 0x1E : {
        uint8_t data = GetByte(curOffset++);
        if (GetVersion() < VER_171)
          AddMarker(beginOffset,
                    curOffset - beginOffset,
                    string("resetlfo"),
                    data,
                    0,
                    L"LFO Reset",
                    PRIORITY_MIDDLE,
                    CLR_LFO);
        else
          AddUnknown(beginOffset, curOffset - beginOffset, L"NOP");
      }
      break;
      case 0x1F : {
        uint8_t value = GetByte(curOffset++);
        if (((CPSSeq *) parentSeq)->fmt_version < VER_116) {
          AddBankSelectNoItem(2 + (value / 128));
          AddProgramChange(beginOffset, curOffset - beginOffset, value % 128);
        }
        else {
          bank = value;
          AddBankSelectNoItem(bank * 2);
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"Bank Change", L"", CLR_PROGCHANGE);
        }

        break;
      }

      case 0x20 :
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0x21 :
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0x22 :
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0x23 :
        curOffset++;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;

      default :
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"UNKNOWN", L"", CLR_UNRECOGNIZED);
    }
  }
  return true;
}
