/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "CPSTrackV1.h"
#include "ScaleConversion.h"
#include "SeqEvent.h"

// *************
// CPSTrackV1
// *************


CPSTrackV1::CPSTrackV1(CPSSeq *parentSeq, CPSSynth channelSynth, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length), channelSynth(channelSynth) {
  CPSTrackV1::resetVars();
}

void CPSTrackV1::resetVars() {
  noteDuration = 0xFF;        // verified in progear (required there as well)
  bPrevNoteTie = false;
  prevTieNote = 0;
  curDeltaTable = 0;
  noteState = 0;
  bank = 0;
  portamentoCentsPerSec = 0;
  prevPortamentoDuration = 0;
  memset(loop, 0, sizeof(loop));
  memset(loopOffset, 0, sizeof(loopOffset));
  SeqTrack::resetVars();
}

void CPSTrackV1::addInitialMidiEvents(int trackNum) {
  SeqTrack::addInitialMidiEvents(trackNum);
  addPortamentoTime14BitNoItem(0);
}

void CPSTrackV1::calculateAndAddPortamentoTimeNoItem(int8_t noteDistance) {
  // Portamento time will be expressed in milliseconds
  uint16_t durationInMillis = 0;
  if (portamentoCentsPerSec > 0) {
    uint16_t centDistance = abs(noteDistance) * 100;
    durationInMillis = (static_cast<double>(centDistance) / static_cast<double>(portamentoCentsPerSec)) * 1000.0;
  }
  if (durationInMillis == prevPortamentoDuration) {
    return;
  }
  prevPortamentoDuration = durationInMillis;
  addPortamentoTime14BitNoItem(durationInMillis);
}

bool CPSTrackV1::readEvent() {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);

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
    uint32_t delta = delta_table[curDeltaTable][((status_byte >> 5) & 7) - 1];

    //if it's not a rest
    if ((status_byte & 0x1F) != 0) {
      uint32_t absDur = static_cast<uint32_t>((delta / 256.0) * noteDuration);

      if (channelSynth == CPSSynth::OKIM6295) {
        // OKIM6295 doesn't control pitch, so we'll use middle C for all notes
        key = 0x3C;
      } else {
        key = (status_byte & 0x1F) + octave_table[noteState & 0x0F] - 1;
      }

      // Tie note
      if ((noteState & 0x40) > 0) {
        if (!bPrevNoteTie) {
          addNoteOn(beginOffset, curOffset - beginOffset, key, 127, "Note On (tied / with portamento)");
          addPortamentoNoItem(true);
        }
        else if (key != prevTieNote) {
          calculateAndAddPortamentoTimeNoItem(key - prevTieNote);
          addNoteOn(beginOffset, curOffset - beginOffset, key, 127, "Note On (tied)");
          addNoteOffNoItem(prevTieNote);
        }
        else
          addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", "", CLR_NOTEON);
        bPrevNoteTie = true;
        prevTieNote = key;
      }
      else {
        if (bPrevNoteTie) {
          if (key != prevTieNote) {
            calculateAndAddPortamentoTimeNoItem(key - prevTieNote);
            addNoteByDur(beginOffset, curOffset - beginOffset, key, 127, absDur);
            addNoteOffNoItem(prevTieNote);
            insertPortamentoNoItem(false, getTime()+absDur);
          }
          else {
            addTime(absDur);
            delta -= absDur;
            addNoteOff(beginOffset, curOffset - beginOffset, prevTieNote, "Note Off (tied)");
            addPortamentoNoItem(false);
          }
        }
        else {
          addNoteByDur(beginOffset, curOffset - beginOffset, key, 127, absDur);
        }
        bPrevNoteTie = false;
      }
    }
    else {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Rest", "", CLR_REST);
    }
    addTime(delta);
  }
  else {
    int loopNum;
    switch (status_byte) {
      case 0x00 :
        noteState ^= 0x20;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Note State xor 0x20 (change duration table)",
                        "",
                        CLR_CHANGESTATE);
        break;
      case 0x01 :
        noteState ^= 0x40;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Note State xor 0x40 (Toggle tie)",
                        "",
                        CLR_CHANGESTATE);
        break;
      case 0x02 :
        noteState |= (1 << 4);
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Note State |= 0x10 (change duration table)",
                        "",
                        CLR_CHANGESTATE);
        break;
      case 0x03 :
        noteState ^= 8;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Note State xor 8 (change octave)",
                        "",
                        CLR_CHANGESTATE);
        break;

      case 0x04 :
        noteState &= 0x97;
        noteState |= readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Change Note State (& 0x97)", "", CLR_CHANGESTATE);
        break;

      case 0x05 : {
        if ((static_cast<CPSSeq*>(parentSeq))->fmt_version >= VER_140) {
          // This byte is clearly the desired BPM, however there is a loss of resolution when the driver
          // converts this value because it is represented with 16 bits... See the table in sfa3 at 0x3492.
          // I've decided to keep the desired BPM rather than use the exact tempo value from the table
          uint8_t tempo = readByte(curOffset++);
          addTempoBPM(beginOffset, curOffset - beginOffset, tempo);
        }
        else {
          // In versions < 1.40, the tempo value is represented as the number of ticks to subtract
          // from a ticks-to-next-event state variable every iteration of the driver. We know the
          // driver iterates at (250 / 4) hz (see CPS2_DRIVER_RATE_HZ). We also know the PPQN aligns
          // with 48 ticks per beat, but driver timing is done with 16 bits of resolution,
          // so there are 48 << 8 internal ticks per beat.
          // We want to get the microseconds per beat, as this is how MIDI represents tempo.
          // First we calculate the iterations per beat: (48 << 8) / ticks per iteration
          // Then we calculate the seconds per beat: iterations per beat / DRIVER_RATE_IN_HZ
          // Convert to microseconds and we're good to go.
          uint16_t ticks_per_iteration = getShortBE(curOffset);
          curOffset += 2;
          auto internal_ppqn = parentSeq->ppqn() << 8;
          auto iterations_per_beat = static_cast<double>(internal_ppqn) / ticks_per_iteration;
          const uint32_t micros_per_beat = lround((iterations_per_beat / CPS2_DRIVER_RATE_HZ) * 1000000);
          addTempo(beginOffset, curOffset - beginOffset, micros_per_beat);
        }
        break;
      }

      case 0x06 :
        noteDuration = readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Duration", "", CLR_CHANGESTATE);
        break;

      case 0x07 :
        switch (channelSynth) {
          case QSOUND:
            vol = readByte(curOffset++);
            vol = convertPercentAmpToStdMidiVal(vol_table[vol] / static_cast<double>(0x1FFF));
            this->addVol(beginOffset, curOffset - beginOffset, vol);
            break;
          case OKIM6295: {
            constexpr uint8_t okiAttenTable[16] = { 32, 22, 16, 11, 8, 6, 4, 3, 2, 0, 0, 0, 0, 0, 0, 0};
            vol = 8 - readByte(curOffset++);
            vol = convertPercentAmpToStdMidiVal(static_cast<double>(okiAttenTable[vol & 0xF]/32.0));
            this->addVol(beginOffset, curOffset - beginOffset, vol);
            break;
          }
          case YM2151:
            vol = readByte(curOffset++);
            vol = convertPercentAmpToStdMidiVal(vol_table[vol] / (double) 0x1FFF);
            this->addVol(beginOffset, curOffset - beginOffset, vol);
            break;
        }
        break;

      case 0x08 : {
        uint8_t progNum = readByte(curOffset++);
        //if (((CPSSeq*)parentSeq)->fmt_version < VER_116) {
        addBankSelectNoItem((bank * 2) + (progNum / 128));
        addProgramChange(beginOffset, curOffset - beginOffset, progNum % 128);
        //}
        //else
        //	AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
        break;
      }

      //effectively sets the octave
      case 0x09 :
        noteState &= 0xF8;
        noteState |= readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Octave", "", CLR_CHANGESTATE);
        break;

      // Global Transpose
      case 0x0A : {
        // For now, skip 0x0A on CPS1, I'll come back to it for the YM2151 implementation
        if (channelSynth == CPSSynth::OKIM6295 || channelSynth == CPSSynth::YM2151) {
          curOffset++;
          break;
        }
        int8_t globalTranspose = readByte(curOffset++);
        addGlobalTranspose(beginOffset, curOffset - beginOffset, globalTranspose);
        break;
      }

      //Transpose
      case 0x0B :
        transpose = readByte(curOffset++);
        addTranspose(beginOffset, curOffset - beginOffset, transpose);
        break;

      //Pitch bend - only gets applied at Note-on, but don't care.  It's used mostly as a detune for chorus effect
      // pitch bend value is a signed byte with a range of +/- 50 cents.
      case 0x0C : {
        uint8_t pitchbend = readByte(curOffset++);
        //double cents = (pitchbend / 256.0) * 100;
        addMarker(beginOffset,
                  curOffset - beginOffset,
                  std::string("pitchbend"),
                  pitchbend,
                  0,
                  "Pitch Bend",
                  PRIORITY_MIDDLE,
                  CLR_PITCHBEND);
        //AddPitchBend(beginOffset, curOffset-beginOffset, (cents / 200) * 8192);
      }
      break;
      case 0x0D : {
        // Portamento: take the rate value, left shift it 1.  This value * (100/256) is increment in cents every 1/(250/4) seconds until we hit target key.
        // A portamento rate value of 0 means instantaneous slide
        uint8_t portamentoRate = readByte(curOffset++);
        if (portamentoRate != 0) {
          auto centsPerSecond = static_cast<uint16_t>(static_cast<double>(portamentoRate) * 2 * (100.0/256.0) * (256.0/4.0));
          portamentoCentsPerSec = centsPerSecond;
        } else {
          portamentoCentsPerSec = 0;
        }
        addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento Time", "", CLR_PORTAMENTOTIME);
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
          loop[loopNum] = readByte(curOffset++);    //set the number of times to loop
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

        {
          uint32_t jump;
          if (static_cast<CPSSeq*>(parentSeq)->fmt_version <= VER_CPS1_425) {
            jump = getShortBE(curOffset);
            curOffset += 2;

            // sf2ce seq 0x84 seems to have a bug at D618
            // where it jumps to offset 0. verified with mame debugger.
            if (jump == 0) {
              return false;
            }
          }
          else {
            if ((readByte(curOffset) & 0x80) == 0) {
              uint8_t jumpByte = readByte(curOffset++);
              jump = curOffset - jumpByte;
            } else {
              jump = curOffset + 2 + static_cast<int16_t>(getShortBE(curOffset));
              curOffset += 2;
            }
          }

          addGenericEvent(beginOffset, curOffset - beginOffset, "Loop", "", CLR_LOOP);

          if (loop[loopNum] == 0) {
            bInLoop = false;
            loopOffset[loopNum] = 0;
          }
          else {
            //          printf("%X JUMPING TO %X\n", curOffset, jump);
            curOffset = jump;
          }
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
          noteState |= readByte(curOffset++);
          {
            uint16_t jump = getShortBE(curOffset);
            curOffset += 2;
            addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", "", CLR_LOOP);

            if (static_cast<CPSSeq*>(parentSeq)->fmt_version <= VER_CPS1_425) {
              curOffset = jump;
            }
            else {
              curOffset += static_cast<int16_t>(jump);
            }
          }
        }
        else
          curOffset += 3;
        break;

      // Loop Always
      case 0x16 : {
        uint32_t jump;
        if (static_cast<CPSSeq*>(parentSeq)->fmt_version <= VER_CPS1_425) {
          jump = getShortBE(curOffset);
        }
        else {
          jump = curOffset + 2 + static_cast<int16_t>(getShortBE(curOffset));
        }
        bool should_continue = addLoopForever(beginOffset, 3);
        if (readMode == READMODE_ADD_TO_UI) {
          curOffset += 2;
          if (readByte(curOffset) == 0x17) {
            addEndOfTrack(curOffset, 1);
          }
        }
        curOffset = jump;
        return should_continue;
      }

      case 0x17 :
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;

      // pan
      case 0x18 : {
        //the pan value is b/w 0 and 0x20.  0 - hard left, 0x10 - center, 0x20 - hard right
        uint8_t pan = readByte(curOffset++) * 4;
        if (pan != 0)
          pan--;
        this->addPan(beginOffset, curOffset - beginOffset, pan);
        break;
      }

      case 0x19 :
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Reg9 Event (unknown to MAME)");
        break;

      case 0x1A : {
        vol = readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Master Volume", "", CLR_UNKNOWN);
        //this->AddMasterVol(beginOffset, curOffset-beginOffset, vol);
        break;
      }

      //Vibrato depth...
      case 0x1B :
        if (version() < VER_171) {
          uint8_t vibratoDepth = readByte(curOffset++);
          addMarker(beginOffset,
                    curOffset - beginOffset,
                    std::string("vibrato"),
                    vibratoDepth,
                    0,
                    "Vibrato",
                    PRIORITY_HIGH,
                    CLR_PITCHBEND);
        }
        else {
          // First data byte defines behavior 0-3
          uint8_t type = readByte(curOffset++);
          uint8_t data = readByte(curOffset++);
          switch (type) {
            // vibrato
            case 0:
              addMarker(beginOffset,
                        curOffset - beginOffset,
                        std::string("vibrato"),
                        data,
                        0,
                        "Vibrato",
                        PRIORITY_HIGH,
                        CLR_PITCHBEND);
              break;

            // tremelo
            case 1:
              addMarker(beginOffset,
                        curOffset - beginOffset,
                        std::string("tremelo"),
                        data,
                        0,
                        "Tremelo",
                        PRIORITY_MIDDLE,
                        CLR_EXPRESSION);
              break;

            // LFO rate
            case 2:
              addMarker(beginOffset,
                        curOffset - beginOffset,
                        std::string("lfo"),
                        data,
                        0,
                        "LFO Rate",
                        PRIORITY_MIDDLE,
                        CLR_LFO);
              break;

            // LFO reset
            case 3:
              addMarker(beginOffset,
                        curOffset - beginOffset,
                        std::string("resetlfo"),
                        data,
                        0,
                        "LFO Reset",
                        PRIORITY_MIDDLE,
                        CLR_LFO);
              break;
            default:
              break;
          }
        }
        break;
      case 0x1C :
        if (version() < VER_171) {
          const uint8_t tremeloDepth = readByte(curOffset++);
          addMarker(beginOffset,
                    curOffset - beginOffset,
                    std::string("tremelo"),
                    tremeloDepth,
                    0,
                    "Tremelo",
                    PRIORITY_MIDDLE,
                    CLR_EXPRESSION);
        }
        else {
          // I'm not sure at all about the behavior here, need to test
          curOffset += 2;
          addUnknown(beginOffset, curOffset - beginOffset);
        }
        break;

      // LFO rate (for versions < 1.71)
      case 0x1D : {
        uint8_t rate = readByte(curOffset++);
        if (version() < VER_171)
          addMarker(beginOffset,
                    curOffset - beginOffset,
                    std::string("lfo"),
                    rate,
                    0,
                    "LFO Rate",
                    PRIORITY_MIDDLE,
                    CLR_LFO);
        else
          addUnknown(beginOffset, curOffset - beginOffset, "NOP");
        break;
      }

      // Reset LFO state (for versions < 1.71)
      case 0x1E : {
        uint8_t data = readByte(curOffset++);
        if (version() < VER_171)
          addMarker(beginOffset,
                    curOffset - beginOffset,
                    std::string("resetlfo"),
                    data,
                    0,
                    "LFO Reset",
                    PRIORITY_MIDDLE,
                    CLR_LFO);
        else
          addUnknown(beginOffset, curOffset - beginOffset, "NOP");
      }
      break;
      case 0x1F : {
        uint8_t value = readByte(curOffset++);
        if (static_cast<CPSSeq*>(parentSeq)->fmt_version < VER_116) {
          addBankSelectNoItem(2 + (value / 128));
          addProgramChange(beginOffset, curOffset - beginOffset, value % 128);
        }
        else {
          bank = value;
          addBankSelectNoItem(bank * 2);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Bank Change", "", CLR_PROGCHANGE);
        }

        break;
      }

      case 0x20 :
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0x21 :
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0x22 :
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0x23 :
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      default :
        addGenericEvent(beginOffset, curOffset - beginOffset, "UNKNOWN", "", CLR_UNRECOGNIZED);
    }
  }
  return true;
}
