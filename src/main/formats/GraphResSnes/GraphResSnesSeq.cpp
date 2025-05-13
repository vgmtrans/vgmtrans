/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "GraphResSnesSeq.h"
#include "ScaleConversion.h"

using namespace std;

DECLARE_FORMAT(GraphResSnes);

static constexpr int MAX_TRACKS = 8;
static constexpr uint16_t SEQ_PPQN = 48;
static constexpr uint8_t NOTE_VELOCITY = 100;

//  ***************
//  GraphResSnesSeq
//  ***************

GraphResSnesSeq::GraphResSnesSeq(RawFile *file, GraphResSnesVersion ver, uint32_t seqdataOffset, std::string name)
    : VGMSeq(GraphResSnesFormat::name, file, seqdataOffset, 0, std::move(name)), version(ver) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  setAlwaysWriteInitialTempo(60000000.0 / ((125 * 0x85) * SEQ_PPQN)); // good ol' frame-based sequence!

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

GraphResSnesSeq::~GraphResSnesSeq() {
}

void GraphResSnesSeq::resetVars() {
  VGMSeq::resetVars();
}

bool GraphResSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(dwOffset, 3 * MAX_TRACKS);
  if (dwOffset + header->unLength > 0x10000) {
    return false;
  }

  uint32_t curOffset = dwOffset;
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    std::stringstream trackName;
    trackName << "Track Pointer " << (trackIndex + 1);

    bool trackUsed = (readByte(curOffset) != 0);
    if (trackUsed) {
      header->addChild(curOffset, 1, "Enable Track");
    }
    else {
      header->addChild(curOffset, 1, "Disable Track");
    }
    header->addChild(curOffset + 1, 2, trackName.str());
    curOffset += 3;
  }

  return true;
}


bool GraphResSnesSeq::parseTrackPointers(void) {
  uint32_t curOffset = dwOffset;
  uint16_t addrTrackBase = readShort(dwOffset + 1);
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    bool trackUsed = (readByte(curOffset++) != 0);
    uint16_t addrTrackStartVirt = readShort(curOffset);
    curOffset += 2;
    uint16_t addrTrackStart = 24 + addrTrackStartVirt - addrTrackBase + dwOffset;

    if (trackUsed) {
      GraphResSnesTrack *track = new GraphResSnesTrack(this, addrTrackStart);
      aTracks.push_back(track);
    }
  }

  return true;
}

void GraphResSnesSeq::loadEventMap() {
  int statusByte;

  for (statusByte = 0x00; statusByte <= 0x7f; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  for (statusByte = 0x80; statusByte <= 0x8f; statusByte++) {
    EventMap[statusByte] = EVENT_INSTANT_VOLUME;
  }

  for (statusByte = 0x90; statusByte <= 0x9f; statusByte++) {
    EventMap[statusByte] = EVENT_INSTANT_OCTAVE;
  }

  //EventMap[0xe0] = (GraphResSnesSeqEventType)0;
  //EventMap[0xe1] = (GraphResSnesSeqEventType)0;
  //EventMap[0xe2] = (GraphResSnesSeqEventType)0;
  //EventMap[0xe3] = (GraphResSnesSeqEventType)0;
  EventMap[0xe4] = EVENT_TRANSPOSE;
  EventMap[0xe5] = EVENT_MASTER_VOLUME;
  EventMap[0xe6] = EVENT_ECHO_VOLUME;
  EventMap[0xe7] = EVENT_DEC_OCTAVE;
  EventMap[0xe8] = EVENT_INC_OCTAVE;
  EventMap[0xe9] = EVENT_LOOP_BREAK;
  EventMap[0xea] = EVENT_LOOP_START;
  EventMap[0xeb] = EVENT_LOOP_END;
  EventMap[0xec] = EVENT_DURATION_RATE;
  EventMap[0xed] = EVENT_DSP_WRITE;
  EventMap[0xee] = EVENT_LOOP_AGAIN_NO_NEST;
  EventMap[0xef] = EVENT_UNKNOWN2;
  EventMap[0xf0] = EVENT_NOISE_TOGGLE;
  EventMap[0xf1] = EVENT_VOLUME;
  //EventMap[0xf2] = (GraphResSnesSeqEventType)0;
  EventMap[0xf3] = EVENT_MASTER_VOLUME_FADE;
  EventMap[0xf4] = EVENT_PAN;
  //EventMap[0xf5] = (GraphResSnesSeqEventType)0;
  //EventMap[0xf6] = (GraphResSnesSeqEventType)0;
  EventMap[0xf7] = EVENT_ADSR;
  EventMap[0xf8] = EVENT_RET;
  EventMap[0xf9] = EVENT_CALL;
  EventMap[0xfa] = EVENT_GOTO;
  EventMap[0xfb] = EVENT_UNKNOWN1;
  EventMap[0xfc] = EVENT_PROGCHANGE;
  EventMap[0xfd] = EVENT_DEFAULT_LENGTH;
  EventMap[0xfe] = EVENT_UNKNOWN0;
  EventMap[0xff] = EVENT_END;
}


//  *****************
//  GraphResSnesTrack
//  *****************

GraphResSnesTrack::GraphResSnesTrack(GraphResSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  GraphResSnesTrack::resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void GraphResSnesTrack::resetVars(void) {
  SeqTrack::resetVars();

  prevNoteKey = -1;
  prevNoteSlurred = false;
  octave = 4;
  defaultNoteLength = 1;
  durationRate = 8;
  spcPan = 0;
  spcInstr = 0;
  spcADSR = 0x8fe0;
  callStackPtr = 0;
  loopStackPtr = GRAPHRESSNES_LOOP_LEVEL_MAX; // 0xc0 / 0x30
  for (uint8_t loopLevel = 0; loopLevel < GRAPHRESSNES_LOOP_LEVEL_MAX; loopLevel++) {
    loopCount[loopLevel] = -1;
  }
}

bool GraphResSnesTrack::readEvent(void) {
  GraphResSnesSeq *parentSeq = static_cast<GraphResSnesSeq*>(this->parentSeq);

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  GraphResSnesSeqEventType eventType = static_cast<GraphResSnesSeqEventType>(0);
  std::map<uint8_t, GraphResSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

    case EVENT_NOTE: {
      uint8_t key = statusByte & 15;
      bool hasLength = ((statusByte & 0x10) != 0);

      uint8_t len;
      if (hasLength) {
        len = readByte(curOffset++);
      }
      else {
        len = defaultNoteLength;
      }

      uint8_t durRate = max(durationRate, static_cast<uint8_t>(8)); // rate > 8 will cause unexpected result
      uint8_t dur = max(min(len * durRate / 8, len - 1), 1);

      if (key == 7) {
        addRest(beginOffset, curOffset - beginOffset, len);
        prevNoteKey = -1;
      }
      else if (key == 15) {
        // undefined event
        addUnknown(beginOffset, curOffset - beginOffset);
        addTime(len);
        prevNoteKey = -1;
      }
      else {
        // a note, add hints for instrument
        if (!parentSeq->instrADSRHints.contains(spcInstr)) {
          parentSeq->instrADSRHints[spcInstr] = spcADSR;
        }

        const uint8_t NOTE_KEY_TABLE[16] = {
            0x0c, 0x0e, 0x10, 0x11, 0x13, 0x15, 0x17, 0x6f, // c  d  e  f  g  a  b
            0x0d, 0x0f, 0x10, 0x12, 0x14, 0x16, 0x17, 0x6f, // c+ d+ e  f+ g+ a+ b
        };

        int8_t midiKey = (octave * 12) + NOTE_KEY_TABLE[key];
        if (prevNoteSlurred && midiKey == prevNoteKey) {
          desc = fmt::format("Duration: {:d}", dur);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc, Type::Tie, ICON_NOTE);
          makePrevDurNoteEnd(getTime() + dur);
          addTime(len);
        }
        else {
          addNoteByDur(beginOffset,
                       curOffset - beginOffset,
                       midiKey,
                       NOTE_VELOCITY,
                       dur,
                       hasLength ? "Note with Duration" : "Note");
          addTime(len);
        }

        prevNoteKey = midiKey;
      }

      // handle the next slur/tie event here
      if (curOffset + 1 <= 0x10000 && readByte(curOffset) == 0xfe) {
        prevNoteSlurred = true;
      }
      else {
        prevNoteSlurred = false;
      }

      break;
    }

    case EVENT_INSTANT_VOLUME: {
      uint8_t vol = statusByte & 15;
      addVol(beginOffset, curOffset - beginOffset, vol);
      break;
    }

    case EVENT_INSTANT_OCTAVE: {
      octave = statusByte & 15;
      desc = fmt::format("Octave {:d}", octave);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Octave", desc, Type::ChangeState);
      break;
    }

    case EVENT_TRANSPOSE: {
      int8_t newTranspose = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, newTranspose);
      break;
    }

    case EVENT_MASTER_VOLUME: {
      int8_t newVolL = readByte(curOffset++);
      int8_t newVolR = readByte(curOffset++);
      int8_t newVol = min(abs((int) newVolL) + abs((int) newVolR), 255) / 2; // workaround: convert to mono
      addMasterVol(beginOffset, curOffset - beginOffset, newVol, "Master Volume L/R");
      break;
    }

    case EVENT_ECHO_VOLUME: {
      int8_t newVolL = readByte(curOffset++);
      int8_t newVolR = readByte(curOffset++);
      desc = fmt::format("Left Volume: {:d}  Right Volume: {:d}", newVolL, newVolR);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume", desc, Type::Reverb, ICON_CONTROL);
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

    case EVENT_LOOP_START: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "", desc, Type::Loop, ICON_STARTREP);

      if (loopStackPtr == 0) {
        // stack overflow
        bContinue = false;
        break;
      }

      loopStackPtr--;
      break;
    }

    case EVENT_LOOP_END: {
      int8_t count = readByte(curOffset++);
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      dest += beginOffset; // relative offset to address
      desc = fmt::format("Times: {:d}  Destination: ${:04X}", count, dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc, Type::Loop, ICON_ENDREP);

      if (loopStackPtr >= GRAPHRESSNES_LOOP_LEVEL_MAX) {
        // access violation
        bContinue = false;
        break;
      }

      if (loopCount[loopStackPtr] != 0) {
        if (loopCount[loopStackPtr] < 0) {
          // start new loop
          loopCount[loopStackPtr] = count;
        }

        // decrease loop count
        loopCount[loopStackPtr]--;
      }

      if (loopCount[loopStackPtr] != 0) {
        // repeat again
        curOffset = dest;
      }
      else {
        // repeat end
        loopCount[loopStackPtr] = -1;
        loopStackPtr++;
      }

      break;
    }

    case EVENT_DURATION_RATE: {
      uint8_t newDurationRate = readByte(curOffset++);
      durationRate = newDurationRate;
      desc = fmt::format("Duration Rate: {:d}/8", newDurationRate);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate", desc, Type::DurationNote);
      break;
    }

    case EVENT_DSP_WRITE: {
      uint8_t dspReg = readByte(curOffset++);
      uint8_t dspValue = readByte(curOffset++);
      desc = fmt::format("Register: ${:02X}  Value: ${:d}", dspReg, dspValue);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Write to DSP", desc, Type::ChangeState);
      break;
    }

    case EVENT_NOISE_TOGGLE: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise On/Off", desc, Type::ChangeState, ICON_CONTROL);
      break;
    }

    case EVENT_VOLUME: {
      int8_t newVol = readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, min(abs((int) newVol), 127));
      break;
    }

    case EVENT_MASTER_VOLUME_FADE: {
      uint8_t vol = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Master Volume Fade",
                      fmt::format("Delta Volume: {:d}", vol),
                      Type::Volume,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PAN: {
      spcPan = readByte(curOffset++);
      int8_t pan = min(max(spcPan, static_cast<int8_t>(-15)), static_cast<int8_t>(15));

      double volumeLeft;
      double volumeRight;
      if (pan >= 0) {
        volumeLeft = (15 - pan) / 15.0;
        volumeRight = 1.0;
      }
      else {
        volumeLeft = 1.0;
        volumeRight = (15 + pan) / 15.0;
      }

      uint8_t midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight);

      // TODO: apply volume scale
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      break;
    }

    case EVENT_ADSR: {
      uint16_t newADSR = readShort(curOffset);
      curOffset += 2;
      spcADSR = newADSR;

      desc = fmt::format("ADSR: {:04X}", newADSR);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr, ICON_CONTROL);
      break;
    }

    case EVENT_RET: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "End Pattern", desc, Type::Loop, ICON_ENDREP);

      if (callStackPtr == 0) {
        // access violation
        bContinue = false;
        break;
      }

      curOffset = callStack[--callStackPtr];
      break;
    }

    case EVENT_CALL: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      dest += beginOffset; // relative offset to address

      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset, 3, "Pattern Play", desc, Type::Loop, ICON_STARTREP);

      if (callStackPtr >= GRAPHRESSNES_CALLSTACK_SIZE) {
        // stack overflow
        bContinue = false;
        break;
      }

      // save loop start address and repeat count
      callStack[callStackPtr++] = curOffset;

      // jump to subroutine address
      curOffset = dest;

      break;
    }

    case EVENT_GOTO: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      dest += beginOffset; // relative offset to address
      desc = fmt::format("Destination: ${:04X}", dest);
      uint32_t length = curOffset - beginOffset;

      curOffset = dest;
      if (!isOffsetUsed(dest)) {
        addGenericEvent(beginOffset, length, "Jump", desc, Type::LoopForever);
      }
      else {
        bContinue = addLoopForever(beginOffset, length, "Jump");
      }
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t newProg = readByte(curOffset++);
      spcInstr = newProg;
      addProgramChange(beginOffset, curOffset - beginOffset, newProg, true);
      break;
    }

    case EVENT_DEFAULT_LENGTH: {
      defaultNoteLength = readByte(curOffset++);
      desc = fmt::format("Duration: {:d}", defaultNoteLength);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Default Note Length", desc, Type::DurationNote);
      break;
    }

    case EVENT_SLUR:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Slur On", desc, Type::Tie);
      break;

    case EVENT_END:
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;

    default: {
      auto descr = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      bContinue = false;
      break;
    }
  }

  //std::ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}
