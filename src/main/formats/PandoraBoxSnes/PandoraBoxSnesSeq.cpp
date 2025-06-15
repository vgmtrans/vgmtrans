#include "PandoraBoxSnesSeq.h"
#include "ScaleConversion.h"
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(PandoraBoxSnes);

static constexpr int MAX_TRACKS = 8;
static constexpr uint8_t NOTE_VELOCITY = 100;

//  *****************
//  PandoraBoxSnesSeq
//  *****************

const uint8_t PandoraBoxSnesSeq::VOLUME_TABLE[16] = {
    0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c,
    0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c,
};

PandoraBoxSnesSeq::PandoraBoxSnesSeq(RawFile *file,
                                     PandoraBoxSnesVersion ver,
                                     uint32_t seqdataOffset,
                                     std::string newName)
    : VGMSeq(PandoraBoxSnesFormat::name, file, seqdataOffset, 0, newName), version(ver) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

PandoraBoxSnesSeq::~PandoraBoxSnesSeq() {
}

void PandoraBoxSnesSeq::resetVars() {
  VGMSeq::resetVars();

  setAlwaysWriteInitialTempo(readByte(dwOffset + 6));
}

bool PandoraBoxSnesSeq::parseHeader() {
  uint32_t curOffset;

  VGMHeader *header = addHeader(dwOffset, 0);
  if (dwOffset + 0x20 > 0x10000) {
    return false;
  }

  addChild(dwOffset + 6, 1, "Tempo");
  addChild(dwOffset + 7, 1, "Timebase");

  uint8_t timebase = readByte(dwOffset + 7);
  assert((timebase % 4) == 0);
  setPPQN(timebase / 4);

  curOffset = dwOffset + 0x10;
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t ofsTrackStart = readShort(curOffset);
    if (ofsTrackStart != 0xffff) {
      auto trackName = fmt::format("Track Pointer {}", trackIndex + 1);
      header->addChild(curOffset, 2, trackName);
    }
    else {
      header->addChild(curOffset, 2, "NULL");
    }
    curOffset += 2;
  }

  return true;
}

bool PandoraBoxSnesSeq::parseTrackPointers() {
  uint32_t curOffset = dwOffset + 0x10;
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t ofsTrackStart = readShort(curOffset);
    curOffset += 2;
    if (ofsTrackStart != 0xffff) {
      uint16_t addrTrackStart = dwOffset + ofsTrackStart;
      PandoraBoxSnesTrack *track = new PandoraBoxSnesTrack(this, addrTrackStart);
      aTracks.push_back(track);
    }
  }
  return true;
}

void PandoraBoxSnesSeq::loadEventMap() {
  int statusByte;

  for (statusByte = 0x00; statusByte <= 0x3f; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  for (statusByte = 0x40; statusByte <= 0x47; statusByte++) {
    EventMap[statusByte] = EVENT_OCTAVE;
  }

  for (statusByte = 0x48; statusByte <= 0x4f; statusByte++) {
    EventMap[statusByte] = EVENT_QUANTIZE;
  }

  for (statusByte = 0x50; statusByte <= 0x5f; statusByte++) {
    EventMap[statusByte] = EVENT_VOLUME_FROM_TABLE;
  }

  for (statusByte = 0x60; statusByte <= 0xdf; statusByte++) {
    EventMap[statusByte] = EVENT_PROGCHANGE;
  }

  EventMap[0xe0] = EVENT_TEMPO;
  EventMap[0xe1] = EVENT_TUNING;
  EventMap[0xe2] = EVENT_TRANSPOSE;
  EventMap[0xe3] = EVENT_PAN;
  EventMap[0xe4] = EVENT_INC_OCTAVE;
  EventMap[0xe5] = EVENT_DEC_OCTAVE;
  EventMap[0xe6] = EVENT_INC_VOLUME;
  EventMap[0xe7] = EVENT_DEC_VOLUME;
  EventMap[0xe8] = EVENT_VIBRATO_PARAM;
  EventMap[0xe9] = EVENT_VIBRATO;
  EventMap[0xea] = EVENT_ECHO_OFF;
  EventMap[0xeb] = EVENT_ECHO_ON;
  EventMap[0xec] = EVENT_LOOP_START;
  EventMap[0xed] = EVENT_LOOP_END;
  EventMap[0xee] = EVENT_LOOP_BREAK;
  EventMap[0xef] = EVENT_NOP;
  EventMap[0xf0] = EVENT_NOP;
  EventMap[0xf1] = EVENT_DSP_WRITE;
  EventMap[0xf2] = EVENT_NOISE_PARAM;
  EventMap[0xf3] = EVENT_ADSR;
  EventMap[0xf4] = EVENT_UNKNOWN1;
  EventMap[0xf5] = EVENT_END;
  EventMap[0xf6] = EVENT_VOLUME;
}


//  *****************
//  PandoraBoxSnesTrack
//  *****************

PandoraBoxSnesTrack::PandoraBoxSnesTrack(PandoraBoxSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void PandoraBoxSnesTrack::resetVars() {
  SeqTrack::resetVars();

  octave = 3;
  prevNoteKey = -1;
  prevNoteSlurred = false;
  spcNoteLength = 1; // just in case
  spcNoteQuantize = 0;
  spcVolumeIndex = 15;
  spcInstr = 0;
  spcADSR = 0x8fe0;
  spcCallStackPtr = 0;
}

bool PandoraBoxSnesTrack::readEvent() {
  PandoraBoxSnesSeq *parentSeq = (PandoraBoxSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  PandoraBoxSnesSeqEventType eventType = (PandoraBoxSnesSeqEventType) 0;
  std::map<uint8_t, PandoraBoxSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc = describeUnknownEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      uint8_t arg4 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3, arg4);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_NOP: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", "", Type::Nop);
      break;
    }

    case EVENT_NOTE: {
      uint8_t keyIndex = statusByte & 15;
      bool noKeyoff = (statusByte & 0x10) != 0; // i.e. slur/tie
      bool hasLength = (statusByte & 0x20) == 0;

      uint8_t len;
      if (hasLength) {
        len = readByte(curOffset++);
        spcNoteLength = len;
      }
      else {
        len = spcNoteLength;
      }

      uint8_t dur;
      if (noKeyoff || spcNoteQuantize == 0) {
        dur = len;
      }
      else {
        dur = len * spcNoteQuantize / 8;
      }

      bool rest = (keyIndex == 0);
      if (rest) {
        addRest(beginOffset, curOffset - beginOffset, len);
        prevNoteSlurred = false;
      }
      else {
        // a note, add hints for instrument
        if (parentSeq->instrADSRHints.find(spcInstr) == parentSeq->instrADSRHints.end()) {
          parentSeq->instrADSRHints[spcInstr] = spcADSR;
        }

        int8_t key = (octave * 12) + (keyIndex - 1);
        if (prevNoteSlurred && key == prevNoteKey) {
          // tie
          makePrevDurNoteEnd(getTime() + dur);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", "", Type::Tie);
        }
        else {
          // note
          addNoteByDur(beginOffset, curOffset - beginOffset, key, NOTE_VELOCITY, dur);
          prevNoteKey = key;
        }
        prevNoteSlurred = noKeyoff;
        addTime(len);
      }

      break;
    }

    case EVENT_OCTAVE: {
      octave = (statusByte - 0x40);
      desc = fmt::format("Octave: {:d}", octave);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Octave", desc, Type::ChangeState);
      break;
    }

    case EVENT_QUANTIZE: {
      spcNoteQuantize = (statusByte - 0x48);
      desc = fmt::format("Length: {:d}/8", spcNoteQuantize);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate", desc, Type::DurationNote);
      break;
    }

    case EVENT_VOLUME_FROM_TABLE: {
      uint8_t newVolumeIndex = (statusByte - 0x50);

      uint8_t newVolume = getVolume(newVolumeIndex);
      desc = fmt::format("Volume Index: {:d} ({:d})", spcVolumeIndex, newVolume);

      addVol(beginOffset, curOffset - beginOffset, newVolume, "Volume From Table");
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t instrNum = (statusByte - 0x60);
      spcInstr = instrNum;
      addProgramChange(beginOffset, curOffset - beginOffset, instrNum);
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = readByte(curOffset++);
      addTempoBPM(beginOffset, curOffset - beginOffset, newTempo);
      break;
    }

    case EVENT_TUNING: {
      int8_t newTuning = readByte(curOffset++);
      desc = fmt::format("{}{} Hz", (newTuning >= 0 ? "+" : ""), static_cast<int>(newTuning));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Fine Tuning", desc, Type::FineTune);
      break;
    }

    case EVENT_TRANSPOSE: {
      int8_t newTranspose = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, newTranspose);
      break;
    }

    case EVENT_PAN: {
      uint8_t newPan = readByte(curOffset++);

      newPan = std::min(newPan, (uint8_t) 128);
      double linearPan = newPan / 128.0;
      double volumeScale;
      int8_t midiPan = convertLinearPercentPanValToStdMidiVal(linearPan, &volumeScale);

      addPan(beginOffset, curOffset - beginOffset, midiPan);
      addExpressionNoItem(std::round(127.0 * volumeScale));
      break;
    }

    case EVENT_INC_OCTAVE: {
      addIncrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_DEC_OCTAVE: {
      addDecrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_INC_VOLUME: {
      if (spcVolumeIndex < 15) {
        spcVolumeIndex++;
      }

      uint8_t newVolume = getVolume(spcVolumeIndex);
      desc = fmt::format("Volume Index: {:d} ({:d})", spcVolumeIndex, newVolume);

      addVol(beginOffset, curOffset - beginOffset, newVolume, "Increase Volume");
      break;
    }

    case EVENT_DEC_VOLUME: {
      if (spcVolumeIndex > 0) {
        spcVolumeIndex--;
      }

      uint8_t newVolume = getVolume(spcVolumeIndex);
      desc = fmt::format("Volume Index: {:d} ({:d})", spcVolumeIndex, newVolume);

      addVol(beginOffset, curOffset - beginOffset, newVolume, "Decrease Volume");
      break;
    }

    case EVENT_VIBRATO_PARAM: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      uint8_t arg4 = readByte(curOffset++);
      uint8_t arg5 = readByte(curOffset++);

      desc = fmt::format("Arg1: {:d}  Arg2: {:d}  Arg3: {:d}  Arg4: {:d}  Arg5: {:d}",
                         arg1, arg2, arg3, arg4, arg5);

      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Param", desc, Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO: {
      bool vibratoOn = (readByte(curOffset++) != 0);
      desc = fmt::format("Vibrato: {}", vibratoOn ? "On" : "Off");
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato On/Off", desc, Type::Vibrato);
      break;
    }

    case EVENT_ECHO_OFF: {
      addReverb(beginOffset, curOffset - beginOffset, 0, "Echo Off");
      break;
    }

    case EVENT_ECHO_ON: {
      addReverb(beginOffset, curOffset - beginOffset, 40, "Echo On");
      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = readByte(curOffset++);
      desc = fmt::format("Times: {:d}", count);
      addGenericEvent(beginOffset, 2, "Loop Start", desc, Type::RepeatStart);

      if (spcCallStackPtr + 5 > PANDORABOXSNES_CALLSTACK_SIZE) {
        // stack overflow
        bContinue = false;
        break;
      }

      // save loop start address
      spcCallStack[spcCallStackPtr++] = curOffset & 0xff;
      spcCallStack[spcCallStackPtr++] = (curOffset >> 8) & 0xff;
      // save loop end address
      spcCallStack[spcCallStackPtr++] = 0;
      spcCallStack[spcCallStackPtr++] = 0;
      // save loop count
      spcCallStack[spcCallStackPtr++] = count;

      break;
    }

    case EVENT_LOOP_END: {
      if (spcCallStackPtr < 5) {
        // access violation
        bContinue = false;
        break;
      }

      if (spcCallStack[spcCallStackPtr - 1] == 0xff) {
        // infinite loop
        bContinue = addLoopForever(beginOffset, curOffset - beginOffset, "Loop End");
      }
      else {
        // regular loop
        addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", "", Type::RepeatEnd);

        // decrease repeat count (0 becomes an infinite loop, as a result)
        spcCallStack[spcCallStackPtr - 1]--;
      }

      if (spcCallStack[spcCallStackPtr - 1] == 0) {
        // repeat end
        spcCallStackPtr -= 5;
      }
      else {
        // repeat again, set end address
        spcCallStack[spcCallStackPtr - 3] = curOffset & 0xff;
        spcCallStack[spcCallStackPtr - 2] = (curOffset >> 8) & 0xff;

        // back to start address
        curOffset = spcCallStack[spcCallStackPtr - 5] | (spcCallStack[spcCallStackPtr - 4] << 8);
      }

      break;
    }

    case EVENT_LOOP_BREAK: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", "", Type::RepeatEnd);

      if (spcCallStackPtr < 5) {
        // access violation
        bContinue = false;
        break;
      }

      if (spcCallStack[spcCallStackPtr - 1] == 1) {
        curOffset = spcCallStack[spcCallStackPtr - 3] | (spcCallStack[spcCallStackPtr - 2] << 8);
        spcCallStackPtr -= 5;
      }

      break;
    }

    case EVENT_DSP_WRITE: {
      uint8_t dspReg = readByte(curOffset++);
      uint8_t dspValue = readByte(curOffset++);
      desc = fmt::format("Register: ${:02X}  Value: ${:02X}", dspReg, dspValue);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Write to DSP", desc, Type::ChangeState);
      break;
    }

    case EVENT_NOISE_PARAM: {
      uint8_t param = readByte(curOffset++);

      uint8_t newTarget = param & 3;
      std::string targetDesc =
          fmt::format("Channel Type: {}{}", newTarget, newTarget ? "" : " (Keep Current)");

      if ((param & 0x80) == 0) {
        uint8_t newNCK = (param >> 2) & 15;
        desc = fmt::format("{}  Noise Frequency (NCK): {:d}", targetDesc, newNCK);
      } else {
        desc = fmt::format("{}  Noise Frequency (NCK): (Keep Current)", targetDesc);
      }

      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Param", desc, Type::Noise);
      break;
    }

    case EVENT_ADSR: {
      uint8_t arRate = readByte(curOffset++);
      uint8_t drRate = readByte(curOffset++);
      uint8_t srRate = readByte(curOffset++);
      uint8_t slRate = readByte(curOffset++);
      uint8_t xxRate = readByte(curOffset++);

      uint8_t ar = (arRate * 0x0f) / 127;
      uint8_t dr = (drRate * 0x07) / 127;
      uint8_t sl = 7 - ((slRate * 0x07) / 127);
      uint8_t sr = (srRate * 0x1f) / 127;
      spcADSR = ((0x80 | (dr << 4) | ar) << 8) | ((sl << 5) | sr);

      desc = fmt::format(
          "AR: {:d}/127 ({:d})  DR: {:d}/127 ({:d})  SR: {:d}/127 ({:d})  SL: {:d}/127 ({:d})  Arg5: {:d}/127",
          arRate, ar, drRate, dr, srRate, sr, slRate, sl, xxRate);

      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      break;
    }

    case EVENT_END:
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;

    case EVENT_VOLUME: {
      uint8_t newVolumeIndex = readByte(curOffset++);
      spcVolumeIndex = newVolumeIndex;

      uint8_t newVolume = getVolume(newVolumeIndex);
      addVol(beginOffset, curOffset - beginOffset, newVolume);
      break;
    }

    default: {
      auto descr = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }

  //std::ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}

uint8_t PandoraBoxSnesTrack::getVolume(uint8_t volumeIndex) {
  PandoraBoxSnesSeq *parentSeq = (PandoraBoxSnesSeq *) this->parentSeq;

  uint8_t volume;

  if (volumeIndex < 0x10) {
    // read volume from table
    volume = parentSeq->VOLUME_TABLE[volumeIndex];
  }
  else {
    // direct volume
    volume = volumeIndex;
  }

  // actual engine does not limit value,
  // but it probably does not expect value more than $7f
  volume = std::min(volume, (uint8_t) 0x7f);

  return volume;
}
