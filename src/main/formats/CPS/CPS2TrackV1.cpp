/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "CPS2TrackV1.h"
#include "CPSCommon.h"
#include "ScaleConversion.h"
#include "SeqEvent.h"

static const u16 vol_table[128] = {
  0, 0xA, 0x18, 0x26, 0x34, 0x42, 0x51, 0x5F, 0x6E, 0x7D, 0x8C, 0x9B, 0xAA,
  0xBA, 0xC9, 0xD9, 0xE9, 0xF8, 0x109, 0x119, 0x129, 0x13A, 0x14A, 0x15B,
  0x16C, 0x17D, 0x18E, 0x1A0, 0x1B2, 0x1C3, 0x1D5, 0x1E8, 0x1FC, 0x20D, 0x21F,
  0x232, 0x245, 0x259, 0x26C, 0x280, 0x294, 0x2A8, 0x2BD, 0x2D2, 0x2E7, 0x2FC,
  0x311, 0x327, 0x33D, 0x353, 0x36A, 0x381, 0x398, 0x3B0, 0x3C7, 0x3DF, 0x3F8,
  0x411, 0x42A, 0x443, 0x45D, 0x477, 0x492, 0x4AD, 0x4C8, 0x4E4, 0x501, 0x51D,
  0x53B, 0x558, 0x577, 0x596, 0x5B5, 0x5D5, 0x5F5, 0x616, 0x638, 0x65A, 0x67D,
  0x6A1, 0x6C5, 0x6EB, 0x711, 0x738, 0x75F, 0x788, 0x7B2, 0x7DC, 0x808, 0x834,
  0x862, 0x891, 0x8C2, 0x8F3, 0x927, 0x95B, 0x991, 0x9C9, 0xA03, 0xA3F,
  0xA7D, 0xABD, 0xAFF, 0xB44, 0xB8C, 0xBD7, 0xC25, 0xC76, 0xCCC,
  0xD26, 0xD85, 0xDE9, 0xE53, 0xEC4, 0xF3C, 0xFBD, 0x1048, 0x10DF,
  0x1184, 0x123A, 0x1305, 0x13EA, 0x14F1, 0x1625, 0x179B, 0x1974, 0x1BFB, 0x1FFD
};

// *************
// CPSTrackV1
// *************


CPS2TrackV1::CPS2TrackV1(VGMSeq *parentSeq, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length) {
  CPS2TrackV1::resetVars();
}

void CPS2TrackV1::resetVars() {
  noteDuration = 0xFF;        // verified in progear (required there as well)
  bPrevNoteTie = false;
  prevTieNote = 0;
  curDeltaTable = 0;
  noteState = 0;
  bank = 0;
  progNum = -1;
  portamentoCentsPerSec = 0;
  prevPortamentoDuration = 0;
  memset(loop, 0, sizeof(loop));
  memset(loopOffset, 0, sizeof(loopOffset));
  SeqTrack::resetVars();
}

void CPS2TrackV1::addInitialMidiEvents(int trackNum) {
  SeqTrack::addInitialMidiEvents(trackNum);
  addPortamentoTime14BitNoItem(0);
}

void CPS2TrackV1::calculateAndAddPortamentoTimeNoItem(int8_t noteDistance) {
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

bool CPS2TrackV1::readEvent() {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);
  auto cpsSeq = static_cast<CPS2Seq*>(parentSeq);
  u8 masterVol = cpsSeq->masterVolume();

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
      u8 absDur = static_cast<u8>(static_cast<u16>(delta * noteDuration) >> 8);
      key = (status_byte & 0x1F) + octave_table[noteState & 0x0F] - 1;

      // Tie note
      if ((noteState & 0x40) > 0) {
        if (!bPrevNoteTie) {
          addNoteOn(beginOffset, curOffset - beginOffset, key, masterVol, "Note On (tied / with portamento)");
          addPortamentoNoItem(true);
        }
        else if (key != prevTieNote) {
          calculateAndAddPortamentoTimeNoItem(key - prevTieNote);
          addNoteOn(beginOffset, curOffset - beginOffset, key, masterVol, "Note On (tied)");
          addNoteOffNoItem(prevTieNote);
        }
        else
          addTie(beginOffset, curOffset - beginOffset, delta, "Tie");
        bPrevNoteTie = true;
        prevTieNote = key;
      }
      else {
        if (bPrevNoteTie) {
          if (key != prevTieNote) {
            calculateAndAddPortamentoTimeNoItem(key - prevTieNote);
            addNoteByDur(beginOffset, curOffset - beginOffset, key, masterVol, absDur);
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
          addNoteByDur(beginOffset, curOffset - beginOffset, key, masterVol, absDur);
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
        if (version() >= CPS2_V140) {
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
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Duration", "", Type::ChangeState);
        break;

      case 0x07 : {
        u8 volIndex = readByte(curOffset++);
        double volPercent = vol_table[volIndex] / static_cast<double>(0x1FFF);
        addVol(beginOffset, curOffset - beginOffset, volPercent, Resolution::FourteenBit);
        break;
      }

      case 0x08 : {
        uint8_t progNum = readByte(curOffset++);
        addBankSelectNoItem((bank * 2) + (progNum / 128));
        addProgramChange(beginOffset, curOffset - beginOffset, progNum % 128);
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

      //Transpose
      case 0x0B :
        transpose = readByte(curOffset++);
        addTranspose(beginOffset, curOffset - beginOffset, transpose);
        break;

      //Pitch bend - only gets applied at Note-on, but don't care.  It's used mostly as a detune for chorus effect
      // pitch bend value is a signed byte with a range of +/- 50 cents.
      case 0x0C : {
        uint8_t pitchbend = readByte(curOffset++);
        addMarker(beginOffset,
                  curOffset - beginOffset,
                  std::string("pitchbend"),
                  pitchbend,
                  0,
                  "Pitch Bend",
                  PRIORITY_MIDDLE,
                  Type::PitchBend);
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

        //else if (/*GetVersion() == VER_101 &&*/ loop[loopNum] > GetByte(curOffset))	// only seen in VER_101 so far (Punisher)
        //	return false;

        //already engaged in loop - decrement loop counter
        else {
          loop[loopNum]--;
          curOffset++;
        }

        {
          uint32_t jump;
          if ((readByte(curOffset) & 0x80) == 0) {
            uint8_t jumpByte = readByte(curOffset++);
            jump = curOffset - jumpByte;
          } else {
            jump = curOffset + 2 + static_cast<int16_t>(getShortBE(curOffset));
            curOffset += 2;
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
            u16 jump = getShortBE(curOffset);
            curOffset += 2;
            addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", "", Type::Loop);
            curOffset += static_cast<int16_t>(jump);
          }
        }
        else
          curOffset += 3;
        break;

      // Loop Always
      case 0x16 : {
        uint32_t jump;
        jump = curOffset + 2 + static_cast<int16_t>(getShortBE(curOffset));
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
        addUnknown(beginOffset, curOffset - beginOffset, "QSound effect depth?");
        break;

      case 0x1A : {
        uint8_t masterVol = readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Master Volume", "", Type::Unknown);
        // addMasterVol(beginOffset, curOffset-beginOffset, masterVol);
        break;
      }

      //Vibrato depth...
      case 0x1B :
        if (version() < CPS2_V171) {
          uint8_t vibratoDepth = readByte(curOffset++);
          addMarker(beginOffset,
                    curOffset - beginOffset,
                    std::string("vibrato"),
                    vibratoDepth,
                    0,
                    "Vibrato",
                    PRIORITY_HIGH,
                    Type::PitchBend);
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
                        Type::PitchBend);
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
                        Type::Expression);
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
                        Type::Lfo);
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
                        Type::Lfo);
              break;
            default:
              break;
          }
        }
        break;
      case 0x1C:
        if (version() < CPS2_V171) {
          const uint8_t tremeloDepth = readByte(curOffset++);
          addMarker(beginOffset,
                    curOffset - beginOffset,
                    std::string("tremelo"),
                    tremeloDepth,
                    0,
                    "Tremelo",
                    PRIORITY_MIDDLE,
                    Type::Expression);
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
        if (version() < CPS2_V171)
          addMarker(beginOffset,
                    curOffset - beginOffset,
                    std::string("lfo"),
                    rate,
                    0,
                    "LFO Rate",
                    PRIORITY_MIDDLE,
                    Type::Lfo);
        else
          addUnknown(beginOffset, curOffset - beginOffset, "NOP");
        break;
      }

      // Reset LFO state (for versions < 1.71)
      case 0x1E : {
        uint8_t data = readByte(curOffset++);
        if (version() < CPS2_V171)
          addMarker(beginOffset,
                    curOffset - beginOffset,
                    std::string("resetlfo"),
                    data,
                    0,
                    "LFO Reset",
                    PRIORITY_MIDDLE,
                    Type::Lfo);
        else
          addUnknown(beginOffset, curOffset - beginOffset, "NOP");
      }
      break;
      case 0x1F : {
        uint8_t value = readByte(curOffset++);
        if (version() < CPS2_V116) {
          addBankSelectNoItem(2 + (value / 128));
          addProgramChange(beginOffset, curOffset - beginOffset, value % 128);
        }
        else {
          bank = value;
          addBankSelectNoItem(bank * 2);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Bank Change", "", Type::ProgramChange);
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
        addGenericEvent(beginOffset, curOffset - beginOffset, "UNKNOWN", "", Type::Unrecognized);
    }
  }
  return true;
}
