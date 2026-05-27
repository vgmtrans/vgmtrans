#include "Types.h"
#include "PandoraBoxSnesSeq.h"
#include "ScaleConversion.h"
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(PandoraBoxSnes);

static constexpr int MAX_TRACKS = 8;
static constexpr u8 NOTE_VELOCITY = 100;

//  *****************
//  PandoraBoxSnesSeq
//  *****************

const u8 PandoraBoxSnesSeq::VOLUME_TABLE[16] = {
    0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c,
    0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c,
};

PandoraBoxSnesSeq::PandoraBoxSnesSeq(RawFile *file,
                                     PandoraBoxSnesVersion ver,
                                     u32 seqdataOffset,
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

  setAlwaysWriteInitialTempo(readByte(offset() + 6));
}

bool PandoraBoxSnesSeq::parseHeader() {
  u32 curOffset;

  VGMHeader *header = addHeader(offset(), 0);
  if (offset() + 0x20 > 0x10000) {
    return false;
  }

  addChild(offset() + 6, 1, "Tempo");
  addChild(offset() + 7, 1, "Timebase");

  u8 timebase = readByte(offset() + 7);
  assert((timebase % 4) == 0);
  setPPQN(timebase / 4);

  curOffset = offset() + 0x10;
  for (u8 trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    u16 ofsTrackStart = readShort(curOffset);
    if (ofsTrackStart != 0xffff) {
      auto trackName = fmt::format("Track Pointer {:d}", trackIndex + 1);
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
  u32 curOffset = offset() + 0x10;
  for (u8 trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    u16 ofsTrackStart = readShort(curOffset);
    curOffset += 2;
    if (ofsTrackStart != 0xffff) {
      u16 addrTrackStart = offset() + ofsTrackStart;
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

PandoraBoxSnesTrack::PandoraBoxSnesTrack(PandoraBoxSnesSeq *parentFile, u32 offset, u32 length)
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

  u32 beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  u8 statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  PandoraBoxSnesSeqEventType eventType = (PandoraBoxSnesSeqEventType) 0;
  std::map<u8, PandoraBoxSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc = describeUnknownEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;

    case EVENT_UNKNOWN1: {
      u8 arg1 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN2: {
      u8 arg1 = readByte(curOffset++);
      u8 arg2 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN3: {
      u8 arg1 = readByte(curOffset++);
      u8 arg2 = readByte(curOffset++);
      u8 arg3 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN4: {
      u8 arg1 = readByte(curOffset++);
      u8 arg2 = readByte(curOffset++);
      u8 arg3 = readByte(curOffset++);
      u8 arg4 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3, arg4);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_NOP: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", "", Type::Nop);
      break;
    }

    case EVENT_NOTE: {
      u8 keyIndex = statusByte & 15;
      bool noKeyoff = (statusByte & 0x10) != 0; // i.e. slur/tie
      bool hasLength = (statusByte & 0x20) == 0;

      u8 len;
      if (hasLength) {
        len = readByte(curOffset++);
        spcNoteLength = len;
      }
      else {
        len = spcNoteLength;
      }

      u8 dur;
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

        s8 key = (octave * 12) + (keyIndex - 1);
        if (prevNoteSlurred && key == prevNoteKey) {
          // tie
          makePrevDurNoteEnd(getTime() + dur);
          addTie(beginOffset, curOffset - beginOffset, dur, "Tie");
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
      u8 newVolumeIndex = (statusByte - 0x50);

      u8 newVolume = getVolume(newVolumeIndex);
      desc = fmt::format("Volume Index: {:d} ({:d})", spcVolumeIndex, newVolume);

      addVol(beginOffset, curOffset - beginOffset, newVolume, "Volume From Table");
      break;
    }

    case EVENT_PROGCHANGE: {
      u8 instrNum = (statusByte - 0x60);
      spcInstr = instrNum;
      addProgramChange(beginOffset, curOffset - beginOffset, instrNum);
      break;
    }

    case EVENT_TEMPO: {
      u8 newTempo = readByte(curOffset++);
      addTempoBPM(beginOffset, curOffset - beginOffset, newTempo);
      break;
    }

    case EVENT_TUNING: {
      s8 newTuning = readByte(curOffset++);
      desc = fmt::format("{}{:d} Hz", (newTuning >= 0 ? "+" : ""), newTuning);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Fine Tuning", desc, Type::FineTune);
      break;
    }

    case EVENT_TRANSPOSE: {
      s8 newTranspose = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, newTranspose);
      break;
    }

    case EVENT_PAN: {
      u8 newPan = readByte(curOffset++);

      newPan = std::min(newPan, (u8) 128);
      double linearPan = newPan / 128.0;
      double volumeScale;
      s8 midiPan = convertLinearPercentPanValToStdMidiVal(linearPan, &volumeScale);

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

      u8 newVolume = getVolume(spcVolumeIndex);
      desc = fmt::format("Volume Index: {:d} ({:d})", spcVolumeIndex, newVolume);

      addVol(beginOffset, curOffset - beginOffset, newVolume, "Increase Volume");
      break;
    }

    case EVENT_DEC_VOLUME: {
      if (spcVolumeIndex > 0) {
        spcVolumeIndex--;
      }

      u8 newVolume = getVolume(spcVolumeIndex);
      desc = fmt::format("Volume Index: {:d} ({:d})", spcVolumeIndex, newVolume);

      addVol(beginOffset, curOffset - beginOffset, newVolume, "Decrease Volume");
      break;
    }

    case EVENT_VIBRATO_PARAM: {
      u8 arg1 = readByte(curOffset++);
      u8 arg2 = readByte(curOffset++);
      u8 arg3 = readByte(curOffset++);
      u8 arg4 = readByte(curOffset++);
      u8 arg5 = readByte(curOffset++);

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
      u8 count = readByte(curOffset++);
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
      u8 dspReg = readByte(curOffset++);
      u8 dspValue = readByte(curOffset++);
      desc = fmt::format("Register: ${:02X}  Value: ${:02X}", dspReg, dspValue);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Write to DSP", desc, Type::ChangeState);
      break;
    }

    case EVENT_NOISE_PARAM: {
      u8 param = readByte(curOffset++);

      u8 newTarget = param & 3;
      std::string targetDesc = fmt::format("Channel Type: {:d}{}", newTarget, newTarget ? "" : " (Keep Current)");

      if ((param & 0x80) == 0) {
        u8 newNCK = (param >> 2) & 15;
        desc = fmt::format("{}  Noise Frequency (NCK): {:d}", targetDesc, newNCK);
      } else {
        desc = fmt::format("{}  Noise Frequency (NCK): (Keep Current)", targetDesc);
      }

      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Param", desc, Type::Noise);
      break;
    }

    case EVENT_ADSR: {
      u8 arRate = readByte(curOffset++);
      u8 drRate = readByte(curOffset++);
      u8 srRate = readByte(curOffset++);
      u8 slRate = readByte(curOffset++);
      u8 xxRate = readByte(curOffset++);

      u8 ar = (arRate * 0x0f) / 127;
      u8 dr = (drRate * 0x07) / 127;
      u8 sl = 7 - ((slRate * 0x07) / 127);
      u8 sr = (srRate * 0x1f) / 127;
      spcADSR = ((0x80 | (dr << 4) | ar) << 8) | ((sl << 5) | sr);

      desc = fmt::format("AR: {:d}/127 ({:d})  DR: {:d}/127 ({:d})  SR: {:d}/127 ({:d})  SL: {:d}/127 ({:d})  Arg5: {:d}/127",
          arRate, ar, drRate, dr, srRate, sr, slRate, sl, xxRate);

      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      break;
    }

    case EVENT_END:
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;

    case EVENT_VOLUME: {
      u8 newVolumeIndex = readByte(curOffset++);
      spcVolumeIndex = newVolumeIndex;

      u8 newVolume = getVolume(newVolumeIndex);
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

u8 PandoraBoxSnesTrack::getVolume(u8 volumeIndex) {
  PandoraBoxSnesSeq *parentSeq = (PandoraBoxSnesSeq *) this->parentSeq;

  u8 volume;

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
  volume = std::min(volume, (u8) 0x7f);

  return volume;
}
