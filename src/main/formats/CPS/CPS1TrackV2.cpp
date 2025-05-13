/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "CPS1TrackV2.h"
#include "CPSCommon.h"
#include "ScaleConversion.h"
#include "SeqEvent.h"

// ***********
// CPS1TrackV2
// ***********

CPS1TrackV2::CPS1TrackV2(VGMSeq *parentSeq, CPSSynth channelSynth, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length), channelSynth(channelSynth) {
  if (channelSynth == CPSSynth::YM2151) {
    synthType = SynthType::YM2151;
  }
  CPS1TrackV2::resetVars();
}

void CPS1TrackV2::resetVars() {
  noteDuration = 0xFF;        // verified in progear (required there as well)
  bPrevNoteTie = false;
  prevTieNote = 0;
  curDeltaTable = 0;
  noteState = 0;
  progNum = -1;
  portamentoCentsPerSec = 0;
  prevPortamentoDuration = 0;
  memset(loop, 0, sizeof(loop));
  memset(loopOffset, 0, sizeof(loopOffset));
  SeqTrack::resetVars();
}

void CPS1TrackV2::addInitialMidiEvents(int trackNum) {
  SeqTrack::addInitialMidiEvents(trackNum);
  addPortamentoTime14BitNoItem(0);
}

void CPS1TrackV2::calculateAndAddPortamentoTimeNoItem(int8_t noteDistance) {
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

bool CPS1TrackV2::readEvent() {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);
  auto cpsSeq = static_cast<CPS1Seq*>(parentSeq);
  u8 masterVol = cpsSeq->masterVolume();

  if (status_byte >= 0x20) {

    if ((noteState & 0x30) == 0)
      curDeltaTable = 1;
    else if ((noteState & 0x10) == 0)
      curDeltaTable = 0;
    else {
      noteState &= ~(1 << 4);
      curDeltaTable = 2;
    }

    // effectively, use the highest 3 bits of the status byte as index to delta_table.
    uint32_t delta = delta_table[curDeltaTable][((status_byte >> 5) & 7) - 1];

    //if it's not a rest
    if ((status_byte & 0x1F) != 0) {
      u8 absDur = static_cast<u8>(static_cast<u16>(delta * noteDuration) >> 8);

      if (channelSynth == CPSSynth::OKIM6295) {
        if (version() >= CPS1_V500) {
          s8 newProgNum = (status_byte & 0x1F) + octave_table[noteState & 0x0F] - 1;
          if (progNum != newProgNum) {
            progNum = newProgNum;
            addProgramChangeNoItem(newProgNum, false);
          }
        }
        // OKIM6295 doesn't control pitch, so we'll use middle C for all notes
        key = 0x3C;
      } else {
        key = (status_byte & 0x1F) + octave_table[noteState & 0x0F] + 12 - 1;
      }

      // Tie note
      if ((noteState & 0x40) > 0) {
        if (!bPrevNoteTie) {
          addNoteOn(beginOffset, curOffset - beginOffset, key, masterVol, "Note On (tied / with portamento)");
          addPortamentoNoItem(true);
        }
        else if (key != prevTieNote) {
          calculateAndAddPortamentoTimeNoItem(key - prevTieNote);
          addNoteOffNoItem(prevTieNote);
          addNoteOn(beginOffset, curOffset - beginOffset, key, masterVol, "Note On (tied)");
        }
        else
          addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", "", Type::NoteOn);
        bPrevNoteTie = true;
        prevTieNote = key;
      }
      else {
        if (bPrevNoteTie) {
          if (key != prevTieNote) {
            calculateAndAddPortamentoTimeNoItem(key - prevTieNote);
            addNoteOffNoItem(prevTieNote);
            addNoteByDur(beginOffset, curOffset - beginOffset, key, masterVol, absDur - 1);
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
          addNoteByDur(beginOffset, curOffset - beginOffset, key, masterVol, absDur - 1);
        }
        bPrevNoteTie = false;
      }
    }
    else {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Rest", "", Type::Rest);
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
                        Type::ChangeState);
        break;
      case 0x01 :
        noteState ^= 0x40;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Note State xor 0x40 (Toggle tie)",
                        "",
                        Type::ChangeState);
        break;
      case 0x02 :
        noteState |= (1 << 4);
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Note State |= 0x10 (change duration table)",
                        "",
                        Type::ChangeState);
        break;
      case 0x03 :
        noteState ^= 8;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Note State xor 8 (change octave)",
                        "",
                        Type::ChangeState);
        break;

      case 0x04 :
        noteState &= 0x97;
        noteState |= readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Change Note State (& 0x97)", "", Type::ChangeState);
        break;

      case 0x05 : {
        // See the tempo event handler in CPS2TrackV1.cpp for details on how this is calculated.
        uint16_t ticks_per_iteration = getShortBE(curOffset);
        curOffset += 2;
        auto internal_ppqn = parentSeq->ppqn() << 8;
        auto iterations_per_beat = static_cast<double>(internal_ppqn) / ticks_per_iteration;
        const uint32_t micros_per_beat = lround((iterations_per_beat / CPS2_DRIVER_RATE_HZ) * 1000000);
        addTempo(beginOffset, curOffset - beginOffset, micros_per_beat);
        break;
      }

      case 0x06 :
        noteDuration = readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Duration", "", Type::ChangeState);
        break;

      case 0x07 :
        switch (channelSynth) {
          case OKIM6295: {
            constexpr uint8_t okiAttenTable[16] = { 32, 22, 16, 11, 8, 6, 4, 3, 2, 0, 0, 0, 0, 0, 0, 0};
            vol = 8 - readByte(curOffset++);
            vol = convertPercentAmpToStdMidiVal(static_cast<double>(okiAttenTable[vol & 0xF]/32.0));
            this->addVol(beginOffset, curOffset - beginOffset, vol);
            break;
          }
          case YM2151:
            vol = readByte(curOffset++);
            // vol = convertPercentAmpToStdMidiVal(vol_table[vol] / static_cast<double>(0x1FFF));
            this->addVol(beginOffset, curOffset - beginOffset, vol);
            break;
        }
        break;

      case 0x08 : {
        uint8_t progNum = readByte(curOffset++);
        addProgramChange(beginOffset, curOffset - beginOffset, progNum % 128);
        if (channelSynth == CPSSynth::YM2151) {
          cKeyCorrection = cpsSeq->transposeForInstr(progNum);
        }
        break;
      }

      //effectively sets the octave
      case 0x09 :
        noteState &= 0xF8;
        noteState |= readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Octave", "", Type::ChangeState);
        break;

      // Global Transpose
      case 0x0A : {
        int8_t globalTranspose = readByte(curOffset++);
        addGlobalTranspose(beginOffset, curOffset - beginOffset, globalTranspose);
        break;
      }

      // Transpose
      case 0x0B :
        transpose = readByte(curOffset++);
        addTranspose(beginOffset, curOffset - beginOffset, transpose);
        break;

      // Pitch Bend - this value will be sent to the YM2151 Key Fraction register.
      case 0x0C : {
        uint8_t pitchbend = readByte(curOffset++);
        pitchbend >>= 2;
        double cents = pitchbend * 1.587301587301587;
        if (pitchbend >= 32) {
          cents = -100 + cents;
        }
        addPitchBendAsPercent(beginOffset, curOffset-beginOffset, cents / 200.0);
        break;
      }
      case 0x0D : {
        uint8_t portamentoRate = readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento Time", "", Type::PortamentoTime);
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

        //already engaged in loop - decrement loop counter
        else {
          loop[loopNum]--;
          curOffset++;
        }

        {
          uint32_t jump;
          if (version() <= CPS1_V425) {
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

          addGenericEvent(beginOffset, curOffset - beginOffset, "Loop", "", Type::Loop);

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
            addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", "", Type::Loop);

            if (version() <= CPS1_V425) {
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
        if (version() <= CPS1_V425) {
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
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0x1A : {
        uint8_t masterVol = readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Master Volume", "", Type::Unknown);
        cpsSeq->setMasterVolume(masterVol);
        break;
      }

      case 0x1B :
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Vibrato Depth?");
        break;

      case 0x1C:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Tremelo?");
        break;

      case 0x1D:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "LFO Rate?");
        break;

      case 0x1E:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "LFO Reset?");
        break;

      case 0x1F:
      case 0x20:
      case 0x21:
      case 0x22:
      case 0x23:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      default :
        addGenericEvent(beginOffset, curOffset - beginOffset, "UNKNOWN", "", Type::Unrecognized);
    }
  }
  return true;
}
