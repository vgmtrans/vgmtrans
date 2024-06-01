/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "CPSTrackV2.h"
#include "ScaleConversion.h"


CPSTrackV2::CPSTrackV2(CPSSeq *parentSeq, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length) {
  CPSTrackV2::ResetVars();
}

void CPSTrackV2::ResetVars() {
  m_master_volume = 0;
  m_secondary_volume = 0x40;
  memset(loopCounter, 0, sizeof(loopCounter));
  memset(loopOffset, 0, sizeof(loopOffset));
  SeqTrack::ResetVars();
}

uint32_t CPSTrackV2::ReadVarLength() {
  uint32_t delta = 0;
  uint8_t byte;
  do {
    byte = GetByte(curOffset++);
    delta <<= 7;
    delta += byte & 0x7F;
  } while (byte > 0x7F);

  return delta;
}

bool CPSTrackV2::ReadEvent() {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = GetByte(curOffset++);

  // Rest opcodes are [0x00 - 0x7F]
  if (status_byte < 0x80) {

    uint32_t delta = 0;
    uint32_t byte = status_byte;

    // Rest values are var length. They carry to the next byte if the highest bit is NOT set.
    while ((byte & 0x80) == 0) {
      delta <<= 7;
      delta += byte;
      byte = GetByte(curOffset++);
    }
    curOffset--;

    AddRest(beginOffset, curOffset-beginOffset, delta);
    return true;
  }

  // Note opcodes are [0x80 - 0xBF]
  if (status_byte < 0xC0) {
    uint8_t velocity = status_byte & 0x3F;
    uint8_t midiVel = ConvertPercentAmpToStdMidiVal(static_cast<double>(velocity) / static_cast<double>(0x3F));
    uint8_t note = GetByte(curOffset++) & 0x7F;
    uint32_t duration = ReadVarLength();
    AddNoteByDur(beginOffset, curOffset - beginOffset, note, midiVel, duration);
    return true;
  }

  // All other event opcodes are [0xC0 - 0xFF]

  int loopNum;

  switch (static_cast<CPSv2SeqEventType>(status_byte)) {

    case C1_TEMPO: {
      // Shares same logic as version < 1.40.  See comment on tempo in CPSTrackV1 for explanation.
      uint16_t ticksPerIteration = GetShortBE(curOffset);
      curOffset += 2;
      auto internal_ppqn = parentSeq->GetPPQN() << 8;
      auto iterationsPerBeat = static_cast<double>(internal_ppqn) / ticksPerIteration;
      const uint32_t microsPerBeat = lround((iterationsPerBeat / CPS3_DRIVER_RATE_HZ) * 1000000);
      AddTempo(beginOffset, curOffset - beginOffset, microsPerBeat);
      break;
    }

    case C2_SETBANK:
      AddBankSelectNoItem(GetByte(curOffset++) * 2);
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Bank Change", "", CLR_PROGCHANGE);
      break;

    case C3_PITCHBEND: {
      int8_t pitch = GetByte(curOffset++);
      AddMarker(beginOffset,
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
      uint8_t program = GetByte(curOffset++);
      AddProgramChange(beginOffset, curOffset - beginOffset, program);
      break;
    }

    case C5_VIBRATO: {
      uint8_t vibratoDepth = GetByte(curOffset++);
      AddMarker(beginOffset,
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
      m_master_volume = GetByte(curOffset++);
      double volume_percent = m_master_volume * m_secondary_volume / (127.0 * 127.0);
      uint16_t final_volume = ConvertPercentAmpToStd14BitMidiVal(volume_percent);
      AddVolume14Bit(beginOffset, curOffset - beginOffset, final_volume, "Track Master Volume");
      break;
    }

    case C7_PAN: {
      uint8_t pan = GetByte(curOffset++);
      AddPan(beginOffset, curOffset - beginOffset, pan);
      break;
    }

    case EVENT_C8: {
      m_secondary_volume = GetByte(curOffset++);
      double volume_percent = m_master_volume * m_secondary_volume / (127.0 * 127.0);
      uint16_t final_volume = ConvertPercentAmpToStd14BitMidiVal(volume_percent);
      AddVolume14Bit(beginOffset, curOffset - beginOffset, final_volume);
      break;
    }

    case C9_PORTAMENTO:
      curOffset++;
      AddUnknown(beginOffset, curOffset-beginOffset, "Portamento");
      break;

    case EVENT_CA:
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_CB:
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_CC:
      //      curOffset++; //REALLY DOES ONE THING THEN JUMPS TO EVENT CE
      curOffset += 3;
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_CD:
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    case CE_GOTO: {
      int16_t relative_offset = static_cast<int16_t>(GetShortBE(curOffset));
      curOffset += 2;
      auto should_continue = AddLoopForever(beginOffset, curOffset - beginOffset);
      curOffset += relative_offset;
      return should_continue;;
    }

    case EVENT_CF: // jumps to a new offset
      break;

    case D0_LOOP_1_START:
      loopOffset[0] = curOffset;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop 1 Start", "", CLR_LOOP);
      break;

    case D1_LOOP_2_START:
      loopOffset[1] = curOffset;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop 2 Start", "", CLR_LOOP);
      break;

    case D2_LOOP_3_START:
      loopOffset[2] = curOffset;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop 3 Start", "", CLR_LOOP);
      break;

    case D3_LOOP_4_START:
      loopOffset[3] = curOffset;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop 4 Start", "", CLR_LOOP);
      break;

    case D4_LOOP_1: loopNum = 0; goto handleLoop;
    case D5_LOOP_2: loopNum = 1; goto handleLoop;
    case D6_LOOP_3: loopNum = 2; goto handleLoop;
    case D7_LOOP_4: loopNum = 3;
    handleLoop: {

      uint8_t loopCount = GetByte(curOffset++);
      if (loopCount == 0) {
        auto should_continue = AddLoopForever(beginOffset, curOffset - beginOffset);
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
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop", "", CLR_LOOP);
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
        uint16_t jump = GetShortBE(curOffset);
        AddGenericEvent(beginOffset, (curOffset+2) - beginOffset, "Loop Break", "", CLR_LOOP);
        curOffset += jump + 2;
      }
      else {
        curOffset += 2;
      }
      break;

    case EVENT_DC:
      curOffset++;
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    case DD_TRANSPOSE:
      transpose += static_cast<int8_t>(GetByte(curOffset++));
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Transpose", "", CLR_CHANGESTATE);
      break;

    case EVENT_DE:
      curOffset++;
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_DF:
      curOffset++;
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    case E0_RESET_LFO: {
      //TODO: Go back and tweak this logic. It's doing something more.
      uint8_t data = GetByte(curOffset++);
      AddMarker(beginOffset,
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
      uint8_t rate = GetByte(curOffset++);
      AddMarker(beginOffset,
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
      uint8_t tremeloDepth = GetByte(curOffset++);
      AddMarker(beginOffset,
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
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_E4:
      curOffset += 2;
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_E5:
      curOffset += 2;
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    case EVENT_E6:
      curOffset++;
      AddUnknown(beginOffset, curOffset-beginOffset);
      break;

    // NEW IN CPS3 (maybe sfiii2 specifically)
    case E7_FINE_TUNE: {
      const int8_t fine_tune = GetByte(curOffset++);
      auto cents = ((fine_tune - 64)/ 64.0) * 100;
      AddFineTuning(beginOffset, curOffset-beginOffset, cents);
      break;
    }

    // NEW IN CPS3 (maybe sfiii3 specifically)
    // This seems to be used to trigger events in the game itself. For example, this event controls
    // sfiii3's intro animation sequence which is timed to the music.
    case E8_META_EVENT: {
      uint8_t slot = GetByte(curOffset++);
      uint8_t value = GetByte(curOffset++);
      auto description = fmt::format("Slot: {:d} Value: {:d}", slot, value);
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Meta Event", description, CLR_MARKER);
      break;
    }

    case FF_END:
      AddEndOfTrack(beginOffset, curOffset-beginOffset);
      return false;

    default :
      AddGenericEvent(beginOffset, curOffset - beginOffset, "UNKNOWN", "", CLR_UNRECOGNIZED);
      break;
  }

  return true;
}
