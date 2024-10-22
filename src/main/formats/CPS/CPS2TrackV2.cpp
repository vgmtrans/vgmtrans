/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "CPS2TrackV2.h"
#include "ScaleConversion.h"


CPS2TrackV2::CPS2TrackV2(CPS2Seq *parentSeq, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length) {
  CPS2TrackV2::resetVars();
}

void CPS2TrackV2::resetVars() {
  m_master_volume = 0;
  m_secondary_volume = 0x40;
  memset(loopCounter, 0, sizeof(loopCounter));
  memset(loopOffset, 0, sizeof(loopOffset));
  SeqTrack::resetVars();
}

uint32_t CPS2TrackV2::readVarLength() {
  uint32_t delta = 0;
  uint8_t byte;
  do {
    byte = readByte(curOffset++);
    delta <<= 7;
    delta += byte & 0x7F;
  } while (byte > 0x7F);

  return delta;
}

bool CPS2TrackV2::readEvent() {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);

  // Rest opcodes are [0x00 - 0x7F]
  if (status_byte < 0x80) {

    uint32_t delta = 0;
    uint32_t byte = status_byte;

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
    uint8_t velocity = status_byte & 0x3F;
    uint8_t midiVel = convertPercentAmpToStdMidiVal(static_cast<double>(velocity) / static_cast<double>(0x3F));
    uint8_t note = readByte(curOffset++) & 0x7F;
    uint32_t duration = readVarLength();
    addNoteByDur(beginOffset, curOffset - beginOffset, note, midiVel, duration);
    return true;
  }

  // All other event opcodes are [0xC0 - 0xFF]

  int loopNum;

  switch (static_cast<CPSv2SeqEventType>(status_byte)) {

    case C1_TEMPO: {
      // Shares same logic as version < 1.40.  See comment on tempo in CPSTrackV1 for explanation.
      uint16_t ticks_per_iteration = getShortBE(curOffset);
      curOffset += 2;
      auto internal_ppqn = parentSeq->ppqn() << 8;
      auto iterations_per_beat = static_cast<double>(internal_ppqn) / ticks_per_iteration;
      auto fmt_version = static_cast<CPS2Seq*>(parentSeq)->fmt_version;
      const double ITERATIONS_PER_SEC = fmt_version == CPS3 ? CPS3_DRIVER_RATE_HZ : CPS2_DRIVER_RATE_HZ;
      const uint32_t micros_per_beat = lround((iterations_per_beat / ITERATIONS_PER_SEC) * 1000000);
      addTempo(beginOffset, curOffset - beginOffset, micros_per_beat);
      break;
    }

    case C2_SETBANK:
      addBankSelectNoItem(readByte(curOffset++) * 2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Bank Change", "", CLR_PROGCHANGE);
      break;

    case C3_PITCHBEND: {
      int8_t pitch = readByte(curOffset++);
      addMarker(beginOffset,
                curOffset - beginOffset,
                "pitchbend",
                pitch,
                0,
                "Pitch Bend",
                PRIORITY_MIDDLE,
                CLR_PITCHBEND);
      break;
    }

    case C4_PROGCHANGE: {
      uint8_t program = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, program);
      break;
    }

    case C5_VIBRATO: {
      uint8_t vibratoDepth = readByte(curOffset++);
      addMarker(beginOffset,
                curOffset - beginOffset,
                std::string("vibrato"),
                vibratoDepth,
                0,
                "Vibrato",
                PRIORITY_HIGH,
                CLR_PITCHBEND);
      break;
    }

    case C6_TRACK_MASTER_VOLUME: {
      m_master_volume = readByte(curOffset++);
      double volume_percent = m_master_volume * m_secondary_volume / (127.0 * 127.0);
      uint16_t final_volume = convertPercentAmpToStd14BitMidiVal(volume_percent);
      addVolume14Bit(beginOffset, curOffset - beginOffset, final_volume, "Track Master Volume");
      break;
    }

    case C7_PAN: {
      uint8_t pan = readByte(curOffset++);
      addPan(beginOffset, curOffset - beginOffset, pan);
      break;
    }

    case EVENT_C8: {
      m_secondary_volume = readByte(curOffset++);
      double volume_percent = m_master_volume * m_secondary_volume / (127.0 * 127.0);
      uint16_t final_volume = convertPercentAmpToStd14BitMidiVal(volume_percent);
      addVolume14Bit(beginOffset, curOffset - beginOffset, final_volume);
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
      //      curOffset++; //REALLY DOES ONE THING THEN JUMPS TO EVENT CE
      curOffset += 3;
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_CD:
      addUnknown(beginOffset, curOffset-beginOffset);
      break;

    case CE_GOTO: {
      int16_t relative_offset = static_cast<int16_t>(getShortBE(curOffset));
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
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop 1 Start", "", CLR_LOOP);
      break;

    case D1_LOOP_2_START:
      loopOffset[1] = curOffset;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop 2 Start", "", CLR_LOOP);
      break;

    case D2_LOOP_3_START:
      loopOffset[2] = curOffset;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop 3 Start", "", CLR_LOOP);
      break;

    case D3_LOOP_4_START:
      loopOffset[3] = curOffset;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop 4 Start", "", CLR_LOOP);
      break;

    case D4_LOOP_1: loopNum = 0; goto handleLoop;
    case D5_LOOP_2: loopNum = 1; goto handleLoop;
    case D6_LOOP_3: loopNum = 2; goto handleLoop;
    case D7_LOOP_4: loopNum = 3;
    handleLoop: {

      uint8_t loopCount = readByte(curOffset++);
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
        addGenericEvent(beginOffset, curOffset - beginOffset, "Loop", "", CLR_LOOP);
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
        uint16_t jump = getShortBE(curOffset);
        addGenericEvent(beginOffset, (curOffset+2) - beginOffset, "Loop Break", "", CLR_LOOP);
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
      transpose += static_cast<int8_t>(readByte(curOffset++));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Transpose", "", CLR_CHANGESTATE);
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
      uint8_t data = readByte(curOffset++);
      addMarker(beginOffset,
                curOffset - beginOffset,
                "resetlfo",
                data,
                0,
                "LFO Reset",
                PRIORITY_MIDDLE,
                CLR_LFO);
      break;
    }

    case E1_LFO_RATE: {
      uint8_t rate = readByte(curOffset++);
      addMarker(beginOffset,
                curOffset - beginOffset,
                "lfo",
                rate,
                0,
                "LFO Rate",
                PRIORITY_MIDDLE,
                CLR_LFO);
      break;
    }

    case E2_TREMELO: {
      uint8_t tremeloDepth = readByte(curOffset++);
      addMarker(beginOffset,
                curOffset - beginOffset,
                "tremelo",
                tremeloDepth,
                0,
                "Tremelo",
                PRIORITY_MIDDLE,
                CLR_EXPRESSION);
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
      const int8_t fine_tune = readByte(curOffset++);
      auto cents = ((fine_tune - 64)/ 64.0) * 100;
      addFineTuning(beginOffset, curOffset-beginOffset, cents);
      break;
    }

    // NEW IN CPS3 (maybe sfiii3 specifically)
    // This triggers events within the main game logic. For example, this event controls
    // sfiii3's intro animation sequence which is timed to the music. It also determines at what
    // point in a sequence to transition to the variant sequence for round 2 and 3.
    case E8_META_EVENT: {
      uint8_t slot = readByte(curOffset++);
      uint8_t value = readByte(curOffset++);
      auto description = fmt::format("Slot: {:d} Value: {:d}", slot, value);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Meta Event", description, CLR_MARKER);
      break;
    }

    case FF_END:
      addEndOfTrack(beginOffset, curOffset-beginOffset);
      return false;

    default :
      addGenericEvent(beginOffset, curOffset - beginOffset, "UNKNOWN", "", CLR_UNRECOGNIZED);
      break;
  }

  return true;
}
