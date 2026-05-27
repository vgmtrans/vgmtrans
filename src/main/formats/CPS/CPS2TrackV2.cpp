/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "base/types.h"
#include "CPS2TrackV2.h"
#include "scale_conversion.h"


CPS2TrackV2::CPS2TrackV2(CPS2Seq *parentSeq, u32 offset, u32 length)
    : SeqTrack(parentSeq, offset, length) {
  CPS2TrackV2::resetVars();
}

void CPS2TrackV2::resetVars() {
  m_volume = 0;
  m_expression = 0x40;
  memset(loopCounter, 0, sizeof(loopCounter));
  memset(loopOffset, 0, sizeof(loopOffset));
  SeqTrack::resetVars();
}

u32 CPS2TrackV2::readVarLength() {
  u32 delta = 0;
  u8 byte;
  do {
    byte = readByte(curOffset++);
    delta <<= 7;
    delta += byte & 0x7F;
  } while (byte > 0x7F);

  return delta;
}

bool CPS2TrackV2::readEvent() {
  u32 beginOffset = curOffset;
  u8 status_byte = readByte(curOffset++);

  // Rest opcodes are [0x00 - 0x7F]
  if (status_byte < 0x80) {

    u32 delta = 0;
    u32 byte = status_byte;

    // Rest values are var length. They carry to the next byte if the highest bit is NOT set.
    while ((byte & 0x80) == 0) {
      delta <<= 7;
      delta += byte;
      byte = readByte(curOffset++);
    }
    curOffset--;

    addRest(beginOffset, curOffset-beginOffset, delta);
    return true;
  }

  // Note opcodes are [0x80 - 0xBF]
  if (status_byte < 0xC0) {
    u8 velocity = status_byte & 0x3F;
    velocity = (static_cast<double>(velocity) / static_cast<double>(0x3F)) * 127.0;
    u8 note = readByte(curOffset++) & 0x7F;
    u32 duration = readVarLength();
    addNoteByDur(beginOffset, curOffset - beginOffset, note, velocity, duration);
    return true;
  }

  // All other event opcodes are [0xC0 - 0xFF]

  int loopNum;

  switch (static_cast<CPSv2SeqEventType>(status_byte)) {

    case C1_TEMPO: {
      // Shares same logic as version < 1.40.  See comment on tempo in CPSTrackV1 for explanation.
      u16 ticks_per_iteration = getShortBE(curOffset);
      curOffset += 2;
      auto internal_ppqn = parentSeq->ppqn() << 8;
      auto iterations_per_beat = static_cast<double>(internal_ppqn) / ticks_per_iteration;
      auto fmt_version = static_cast<CPS2Seq*>(parentSeq)->fmt_version;
      const double ITERATIONS_PER_SEC = fmt_version == CPS3 ? CPS3_DRIVER_RATE_HZ : CPS2_DRIVER_RATE_HZ;
      const u32 micros_per_beat = lround((iterations_per_beat / ITERATIONS_PER_SEC) * 1000000);
      addTempo(beginOffset, curOffset - beginOffset, micros_per_beat);
      break;
    }

    case C2_SETBANK:
      addBankSelectNoItem(readByte(curOffset++) * 2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Bank Change", "", Type::ProgramChange);
      break;

    case C3_PITCHBEND: {
      s8 pitch = readByte(curOffset++);
      addMarker(beginOffset,
                curOffset - beginOffset,
                "pitchbend",
                pitch,
                0,
                "Pitch Bend",
                PRIORITY_MIDDLE,
                Type::PitchBend);
      break;
    }

    case C4_PROGCHANGE: {
      u8 program = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, program);
      break;
    }

    case C5_VIBRATO: {
      u8 vibratoDepth = readByte(curOffset++);
      addMarker(beginOffset,
                curOffset - beginOffset,
                std::string("vibrato"),
                vibratoDepth,
                0,
                "Vibrato",
                PRIORITY_HIGH,
                Type::PitchBend);
      break;
    }

    case C6_VOLUME: {
      m_volume = readByte(curOffset++);
      // We fold expression into volume to not interfere with our tremolo implementation
      double expression = m_expression > 0 ? m_expression + 1 : 0;
      double volPercent = (m_volume * expression) / (128.0 * 128.0);
      addVol(beginOffset, curOffset - beginOffset, volPercent, Resolution::FourteenBit);
      break;
    }

    case C7_PAN: {
      u8 pan = readByte(curOffset++);
      addPan(beginOffset, curOffset - beginOffset, pan);
      break;
    }

    case C8_EXPRESSION: {
      m_expression = readByte(curOffset++);
      double expression = m_expression > 0 ? m_expression + 1 : 0;
      double volPercent = (m_volume * expression) / (128.0 * 128.0);
      // We fold expression into volume to not interfere with our tremolo implementation
      addVol(beginOffset, curOffset - beginOffset, volPercent, Resolution::FourteenBit, "Expression");
      break;
    }

    case C9_PORTAMENTO:
      curOffset++;
      addUnknown(beginOffset, curOffset-beginOffset, "Portamento");
      break;

    case EVENT_CA:
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_CB:
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_CC:
      // need to check if it's different in CPS2 usage vs CPS3
      curOffset += 3;
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_CD:
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case CE_GOTO: {
      s16 relative_offset = static_cast<s16>(getShortBE(curOffset));
      curOffset += 2;
      auto should_continue = addLoopForever(beginOffset, curOffset - beginOffset);
      if (readMode == READMODE_ADD_TO_UI) {
        if (readByte(curOffset) == 0xFF) {
          addEndOfTrack(curOffset, 1);
        }
      }
      curOffset += relative_offset;
      return should_continue;;
    }

    case EVENT_CF: // jumps to a new offset
      break;

    case D0_LOOP_1_START:
      loopOffset[0] = curOffset;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop 1 Start", "", Type::Loop);
      break;

    case D1_LOOP_2_START:
      loopOffset[1] = curOffset;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop 2 Start", "", Type::Loop);
      break;

    case D2_LOOP_3_START:
      loopOffset[2] = curOffset;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop 3 Start", "", Type::Loop);
      break;

    case D3_LOOP_4_START:
      loopOffset[3] = curOffset;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop 4 Start", "", Type::Loop);
      break;

    case D4_LOOP_1: loopNum = 0; goto handleLoop;
    case D5_LOOP_2: loopNum = 1; goto handleLoop;
    case D6_LOOP_3: loopNum = 2; goto handleLoop;
    case D7_LOOP_4: loopNum = 3;
    handleLoop: {

      u8 loopCount = readByte(curOffset++);
      if (loopCount == 0) {
        auto should_continue = addLoopForever(beginOffset, curOffset - beginOffset);
        if (readMode == READMODE_ADD_TO_UI) {
          if (readByte(curOffset) == 0xFF) {
            addEndOfTrack(curOffset, 1);
          }
        }
        curOffset = loopOffset[loopNum];
        return should_continue;
      }
      loopCounter[loopNum]--;
      if (loopCounter[loopNum] == 0) {      //finished loop
        return true;
      }
      if (loopCounter[loopNum] > 0) {       //still in loop
        curOffset = loopOffset[loopNum];
      }
      else {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Loop", "", Type::Loop);
        loopCounter[loopNum] = loopCount; //start loop
        curOffset = loopOffset[loopNum];
      }
      break;
    }

    case D8_LOOP_1_BREAK: loopNum = 0; goto loopBreak;
    case D9_LOOP_2_BREAK: loopNum = 1; goto loopBreak;
    case DA_LOOP_3_BREAK: loopNum = 2; goto loopBreak;
    case DB_LOOP_4_BREAK: loopNum = 3;
    loopBreak:
      if (loopCounter[loopNum] - 1 == 0) {
        loopCounter[loopNum] = 0;
        u16 jump = getShortBE(curOffset);
        addGenericEvent(beginOffset, (curOffset+2) - beginOffset, "Loop Break", "", Type::Loop);
        curOffset += jump + 2;
      }
      else {
        curOffset += 2;
      }
      break;

    case EVENT_DC:
      curOffset++;
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case DD_TRANSPOSE:
      transpose += static_cast<s8>(readByte(curOffset++));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Transpose", "", Type::ChangeState);
      break;

    case EVENT_DE:
      curOffset++;
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_DF:
      curOffset++;
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case E0_RESET_LFO: {
      //TODO: Go back and tweak this logic. It's doing something more.
      u8 data = readByte(curOffset++);
      addMarker(beginOffset,
                curOffset - beginOffset,
                "resetlfo",
                data,
                0,
                "LFO Reset",
                PRIORITY_MIDDLE,
                Type::Lfo);
      break;
    }

    case E1_LFO_RATE: {
      u8 rate = readByte(curOffset++);
      addMarker(beginOffset,
                curOffset - beginOffset,
                "lfo",
                rate,
                0,
                "LFO Rate",
                PRIORITY_MIDDLE,
                Type::Lfo);
      break;
    }

    case E2_TREMELO: {
      u8 tremeloDepth = readByte(curOffset++);
      addMarker(beginOffset,
                curOffset - beginOffset,
                "tremelo",
                tremeloDepth,
                0,
                "Tremelo",
                PRIORITY_MIDDLE,
                Type::Expression);
      break;
    }

    case EVENT_E3:
      curOffset++;
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_E4:
      curOffset += 2;
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_E5:
      curOffset += 2;
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_E6:
      curOffset++;
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    // NEW IN CPS3 (maybe sfiii2 specifically)
    case E7_FINE_TUNE: {
      const s8 fine_tune = readByte(curOffset++);
      auto cents = ((fine_tune - 64)/ 64.0) * 100;
      addFineTuning(beginOffset, curOffset-beginOffset, cents);
      break;
    }

    // NEW IN CPS3 (maybe sfiii3 specifically)
    // This triggers events within the main game logic. For example, this event controls
    // sfiii3's intro animation sequence which is timed to the music. It also determines at what
    // point in a sequence to transition to the variant sequence for round 2 and 3.
    case E8_META_EVENT: {
      u8 slot = readByte(curOffset++);
      u8 value = readByte(curOffset++);
      auto description = fmt::format("Slot: {:d} Value: {:d}", slot, value);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Meta Event", description, Type::Marker);
      break;
    }

    case FF_END:
      addEndOfTrack(beginOffset, curOffset-beginOffset);
      return false;

    default :
      addGenericEvent(beginOffset, curOffset - beginOffset, "UNKNOWN", "", Type::Unrecognized);
      break;
  }

  return true;
}
