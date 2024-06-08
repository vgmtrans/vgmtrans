/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "MoriSnesSeq.h"

DECLARE_FORMAT(MoriSnes);

//  ***********
//  MoriSnesSeq
//  ***********
#define MAX_TRACKS  10
#define SEQ_PPQN    48

MoriSnesSeq::MoriSnesSeq(RawFile *file, MoriSnesVersion ver, uint32_t seqdataOffset, std::string name)
    : VGMSeq(MoriSnesFormat::name, file, seqdataOffset, 0, std::move(name)),
      version(ver) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

MoriSnesSeq::~MoriSnesSeq() {
}

void MoriSnesSeq::resetVars() {
  VGMSeq::resetVars();

  spcTempo = 0x20;
  fastTempo = false;

  InstrumentAddresses.clear();
  InstrumentHints.clear();
}

bool MoriSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  uint32_t curOffset = dwOffset;
  VGMHeader *header = addHeader(dwOffset, 0);

  // reset track start addresses
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    TrackStartAddress[trackIndex] = 0;
  }

  // parse header events
  while (true) {
    uint32_t beginOffset = curOffset;
    uint8_t statusByte = readByte(curOffset++);
    if (statusByte == 0xff) {
      header->addChild(beginOffset, curOffset - beginOffset, "Header End");
      break;
    }

    if (statusByte <= 0x7f) {
      uint8_t trackIndex = statusByte;
      if (trackIndex > MAX_TRACKS) {
        // out of range
        return false;
      }

      if (curOffset + 2 > 0x10000) {
        return false;
      }

      uint16_t ofsTrackStart = readShort(curOffset);
      curOffset += 2;
      TrackStartAddress[trackIndex] = curOffset + ofsTrackStart;

      std::stringstream trackName;
      trackName << "Track " << (trackIndex + 1) << " Offset";
      header->addChild(beginOffset, curOffset - beginOffset, trackName.str().c_str());
    }
    else {
      header->addUnknownChild(beginOffset, curOffset - beginOffset);
      break;
    }

    if (curOffset + 1 > 0x10000) {
      return false;
    }
  }

  header->setGuessedLength();
  return true;
}

bool MoriSnesSeq::parseTrackPointers() {
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    if (TrackStartAddress[trackIndex] != 0) {
      MoriSnesTrack *track = new MoriSnesTrack(this, TrackStartAddress[trackIndex]);
      aTracks.push_back(track);
    }
  }
  return true;
}

void MoriSnesSeq::loadEventMap() {
  int statusByte;

  for (statusByte = 0x00; statusByte <= 0x7f; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE_PARAM;
  }

  for (statusByte = 0x80; statusByte <= 0x9f; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  for (statusByte = 0xa0; statusByte <= 0xbf; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE_WITH_PARAM;
  }

  EventMap[0xc0] = EVENT_PROGCHANGE;
  EventMap[0xc1] = EVENT_PAN;
  EventMap[0xc2] = EVENT_UNKNOWN1;
  EventMap[0xc3] = EVENT_TEMPO;
  EventMap[0xc4] = EVENT_UNKNOWN1;
  EventMap[0xc5] = EVENT_VOLUME;
  EventMap[0xc6] = EVENT_PRIORITY;
  EventMap[0xc7] = EVENT_TUNING;
  EventMap[0xc8] = EVENT_ECHO_ON;
  EventMap[0xc9] = EVENT_ECHO_OFF;
  EventMap[0xca] = EVENT_ECHO_PARAM;
  EventMap[0xcb] = EVENT_GOTO;
  EventMap[0xcc] = EVENT_CALL;
  EventMap[0xcd] = EVENT_RET;
  EventMap[0xce] = EVENT_LOOP_START;
  EventMap[0xcf] = EVENT_LOOP_END;
  EventMap[0xd0] = EVENT_END;
  EventMap[0xd1] = EVENT_NOTE_NUMBER;
  EventMap[0xd2] = EVENT_OCTAVE_UP;
  EventMap[0xd3] = EVENT_OCTAVE_DOWN;
  EventMap[0xd4] = EVENT_WAIT;
  EventMap[0xd5] = EVENT_UNKNOWN1;
  EventMap[0xd6] = EVENT_PITCHBENDRANGE;
  EventMap[0xd7] = EVENT_TRANSPOSE;
  EventMap[0xd8] = EVENT_TRANSPOSE_REL;
  EventMap[0xd9] = EVENT_TUNING_REL;
  EventMap[0xda] = EVENT_KEY_ON;
  EventMap[0xdb] = EVENT_KEY_OFF;
  EventMap[0xdc] = EVENT_VOLUME_REL;
  EventMap[0xdd] = EVENT_PITCHBEND;
  EventMap[0xde] = EVENT_INSTR;
  EventMap[0xdf] = EVENT_UNKNOWN1;
  EventMap[0xe0] = EVENT_UNKNOWN1;
  EventMap[0xe1] = EVENT_UNKNOWN1;
  EventMap[0xe2] = EVENT_UNKNOWN1;
  EventMap[0xe3] = EVENT_UNKNOWN1;
  EventMap[0xe4] = EVENT_UNKNOWN1;
  EventMap[0xe5] = EVENT_UNKNOWN1;
  EventMap[0xe6] = EVENT_TIMEBASE;
}

double MoriSnesSeq::getTempoInBPM(uint8_t tempo, bool fastTempo) {
  if (tempo != 0) {
    return 60000000.0 / ((SEQ_PPQN / (fastTempo ? 2 : 1)) * (125 * 0x4f)) * (tempo / 256.0);
  }
  else {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}

//  ***************
//  MoriSnesTrack
//  ***************

MoriSnesTrack::MoriSnesTrack(MoriSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  MoriSnesTrack::resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void MoriSnesTrack::resetVars() {
  SeqTrack::resetVars();

  tiedNoteKeys.clear();
  spcDeltaTime = 0;
  spcNoteNumberBase = 0;
  spcNoteDuration = 1;
  spcNoteVelocity = 1;
  spcVolume = 200;
  spcTranspose = 0;
  spcTuning = 0;
  spcCallStackPtr = 0;
}


bool MoriSnesTrack::readEvent() {
  MoriSnesSeq *parentSeq = static_cast<MoriSnesSeq*>(this->parentSeq);

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::stringstream desc;

  // note param
  uint8_t newDelta = spcDeltaTime;
  if (statusByte < 0x80) {
    newDelta = statusByte;
    desc << "Delta Time: " << (int) newDelta;
    if (newDelta != 0) {
      spcDeltaTime = newDelta;
    }

    statusByte = readByte(curOffset);
    if (statusByte < 0x80) {
      spcNoteDuration = statusByte;
      desc << "  Dulation: " << (int) spcNoteDuration;
      curOffset++;

      statusByte = readByte(curOffset);
      if (statusByte < 0x80) {
        spcNoteVelocity = statusByte;
        desc << "  Velocity: " << (int) spcNoteVelocity;
        curOffset++;
      }
    }

    addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc.str().c_str(), CLR_DURNOTE);
    beginOffset = curOffset;
    desc.str("");

    if (curOffset >= 0x10000) {
      return false;
    }

    statusByte = readByte(curOffset++);
  }

  MoriSnesSeqEventType eventType = (MoriSnesSeqEventType) 0;
  std::map<uint8_t, MoriSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      uint8_t arg4 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3
          << "  Arg4: " << (int) arg4;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_NOTE:
    case EVENT_NOTE_WITH_PARAM: {
      uint8_t keyOffset = statusByte & 0x1f;

      const char* eventName;
      if (eventType == EVENT_NOTE_WITH_PARAM) {
        uint8_t noteParam = readByte(curOffset++);
        if (noteParam <= 0x7f) {
          spcNoteDuration = noteParam;
          eventName = "Note with Duration";
        }
        else {
          spcNoteVelocity = (noteParam & 0x7f);
          eventName = "Note with Velocity";
        }
      }
      else {
        eventName = "Note";
      }

      bool tied = (spcNoteDuration == 0);
      uint8_t dur = spcNoteDuration;
      if (tied) {
        dur = spcDeltaTime;
      }

      int8_t key = spcNoteNumberBase + keyOffset;
      auto prevTiedNoteIter = std::find(tiedNoteKeys.begin(), tiedNoteKeys.end(), key);
      if (prevTiedNoteIter != tiedNoteKeys.end()) {
        makePrevDurNoteEnd(getTime() + dur);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc.str().c_str(), CLR_TIE, ICON_NOTE);

        if (!tied) {
          // finish tied note
          tiedNoteKeys.erase(prevTiedNoteIter);
        }
      }
      else {
        addNoteByDur(beginOffset, curOffset - beginOffset, key, spcNoteVelocity, dur, eventName);
        if (tied) {
          // register tied note
          tiedNoteKeys.push_back(key);
        }
      }
      addTime(newDelta);
      break;
    }

    case EVENT_PROGCHANGE: {
      int16_t instrOffset = readShort(curOffset);
      curOffset += 2;
      uint16_t instrAddress = curOffset + instrOffset;
      desc << "Envelope: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) instrAddress;

      uint8_t instrNum;
      for (instrNum = 0; instrNum < parentSeq->InstrumentAddresses.size(); instrNum++) {
        if (parentSeq->InstrumentAddresses[instrNum] == instrAddress) {
          break;
        }
      }

      // new instrument?
      if (instrNum == parentSeq->InstrumentAddresses.size()) {
        parentSeq->InstrumentAddresses.push_back(instrAddress);
        parseInstrument(instrAddress, instrNum);
      }

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Program Change",
                      desc.str().c_str(),
                      CLR_PROGCHANGE,
                      ICON_PROGCHANGE);
      addProgramChangeNoItem(instrNum, false);
      break;
    }

    case EVENT_PAN: {
      int8_t newPan = readByte(curOffset++);
      if (newPan >= 0) {
        if (newPan > 32) {
          // out of range (unexpected behavior)
          newPan = 32;
        }

        uint8_t midiPan = std::min(newPan * 4, 127);
        addPan(beginOffset, curOffset - beginOffset, midiPan);
      }
      else {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Random Pan", desc.str().c_str(), CLR_PAN, ICON_CONTROL);
      }
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = readByte(curOffset++);
      parentSeq->spcTempo = newTempo;
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM(newTempo, parentSeq->fastTempo));
      break;
    }

    case EVENT_VOLUME: {
      uint8_t newVolume = readByte(curOffset++);
      spcVolume = newVolume;
      addVol(beginOffset, curOffset - beginOffset, spcVolume / 2);
      break;
    }

    case EVENT_PRIORITY: {
      uint8_t newPriority = readByte(curOffset++);
      desc << "Priority: " << (int) newPriority;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Priority", desc.str().c_str(), CLR_PRIORITY);
      break;
    }

    case EVENT_TUNING: {
      uint8_t newTuning = readByte(curOffset++);
      spcTuning = newTuning;

      double semitones = spcTuning / 256.0;
      addFineTuning(beginOffset, curOffset - beginOffset, semitones * 100.0);
      break;
    }

    case EVENT_ECHO_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo On", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
      addReverbNoItem(40);
      break;
    }

    case EVENT_ECHO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Off", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
      addReverbNoItem(0);
      break;
    }

    case EVENT_ECHO_PARAM: {
      uint8_t delay = readByte(curOffset++);
      int8_t volume = readByte(curOffset++);
      int8_t feedback = readByte(curOffset++);
      uint8_t filterIndex = readByte(curOffset++);
      desc << "Delay: " << (int) delay << "  FIR: " << (int) volume << "  volume: " << (int) feedback << "  FIR: "
          << (int) filterIndex;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Param",
                      desc.str().c_str(),
                      CLR_REVERB,
                      ICON_CONTROL);
      break;
    }

    case EVENT_GOTO: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      dest += curOffset; // relative offset to address
      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
      uint32_t length = curOffset - beginOffset;

      curOffset = dest;
      if (!isOffsetUsed(dest)) {
        addGenericEvent(beginOffset, length, "Jump", desc.str().c_str(), CLR_LOOPFOREVER);
      }
      else {
        bContinue = addLoopForever(beginOffset, length, "Jump");
      }
      break;
    }

    case EVENT_CALL: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      dest += curOffset; // relative offset to address
      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pattern Play",
                      desc.str().c_str(),
                      CLR_LOOP,
                      ICON_STARTREP);

      if (spcCallStackPtr + 2 > MORISNES_CALLSTACK_SIZE) {
        // stack overflow
        bContinue = false;
        break;
      }

      // save return address
      spcCallStack[spcCallStackPtr++] = curOffset & 0xff;
      spcCallStack[spcCallStackPtr++] = (curOffset >> 8) & 0xff;

      curOffset = dest;

      // TODO: update track address if necessary
      // example: Lennus 2 - Staff Roll $3d47

      break;
    }

    case EVENT_RET: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "End Pattern", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

      if (spcCallStackPtr < 2) {
        // access violation
        bContinue = false;
        break;
      }

      curOffset = spcCallStack[spcCallStackPtr - 2] | (spcCallStack[spcCallStackPtr - 1] << 8);
      spcCallStackPtr -= 2;
      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = readByte(curOffset++);
      desc << "Loop Count: " << (int) count;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

      if (spcCallStackPtr + 3 > MORISNES_CALLSTACK_SIZE) {
        // stack overflow
        bContinue = false;
        break;
      }

      // save loop start address and repeat count
      spcCallStack[spcCallStackPtr++] = curOffset & 0xff;
      spcCallStack[spcCallStackPtr++] = (curOffset >> 8) & 0xff;
      spcCallStack[spcCallStackPtr++] = count;

      break;
    }

    case EVENT_LOOP_END: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

      if (spcCallStackPtr < 3) {
        // access violation
        bContinue = false;
        break;
      }

      uint8_t count = spcCallStack[spcCallStackPtr - 1];
      if (--count == 0) {
        // repeat end, fall through
        spcCallStackPtr -= 3;
      }
      else {
        // repeat again
        spcCallStack[spcCallStackPtr - 1] = count;
        curOffset = spcCallStack[spcCallStackPtr - 3] | (spcCallStack[spcCallStackPtr - 2] << 8);
      }

      break;
    }

    case EVENT_END: {
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;
    }

    case EVENT_NOTE_NUMBER: {
      int8_t newNoteNumber = readByte(curOffset++);
      spcNoteNumberBase = newNoteNumber;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Note Number Base", desc.str().c_str(), CLR_NOTEON);
      break;
    }

    case EVENT_OCTAVE_UP: {
      addIncrementOctave(beginOffset, curOffset - beginOffset);
      spcNoteNumberBase += 12;
      break;
    }

    case EVENT_OCTAVE_DOWN: {
      addDecrementOctave(beginOffset, curOffset - beginOffset);
      spcNoteNumberBase -= 12;
      break;
    }

    case EVENT_WAIT: {
      // do not stop tied note here
      // example: Gokinjo Bouken Tai - Battle (28:0000, Sax at 3rd channel)
      desc << "Duration: " << spcDeltaTime;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wait", desc.str().c_str(), CLR_REST, ICON_REST);
      addTime(spcDeltaTime);
      break;
    }

    case EVENT_PITCHBENDRANGE: {
      uint8_t newRangeSemitones = readByte(curOffset++); // actual range is value/8
      addPitchBendRange(beginOffset,
                        curOffset - beginOffset,
                        (newRangeSemitones / 8) * 100); // range <= 24 is recommended in General MIDI spec
      break;
    }

    case EVENT_TRANSPOSE: {
      int8_t newTranspose = readByte(curOffset++);
      spcTranspose = newTranspose;
      addTranspose(beginOffset, curOffset - beginOffset, spcTranspose);
    }

    case EVENT_TRANSPOSE_REL: {
      int8_t delta = readByte(curOffset++);
      spcTranspose += delta;
      addTranspose(beginOffset, curOffset - beginOffset, spcTranspose, "Transpose (Relative)");
      break;
    }

    case EVENT_TUNING_REL: {
      int8_t delta = readByte(curOffset++);
      spcTuning += delta;

      double semitones = spcTuning / 256.0;
      addFineTuning(beginOffset, curOffset - beginOffset, semitones * 100.0);
      break;
    }

    case EVENT_KEY_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Key On", desc.str().c_str(), CLR_NOTEON, ICON_NOTE);
      break;
    }

    case EVENT_KEY_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Key Off", desc.str().c_str(), CLR_NOTEOFF, ICON_NOTE);
      break;
    }

    case EVENT_VOLUME_REL: {
      int8_t delta = readByte(curOffset++);

      int newVolume = std::min(std::max(spcVolume + delta, 0), 0xff);
      spcVolume += newVolume;

      addVol(beginOffset, curOffset - beginOffset, spcVolume / 2, "Volume (Relative)");
      break;
    }

    case EVENT_PITCHBEND: {
      int8_t pitch = readByte(curOffset++);
      addPitchBend(beginOffset, curOffset - beginOffset, pitch * 64);
      break;
    }

    case EVENT_TIMEBASE: {
      bool fast = ((readByte(curOffset++) & 1) != 0);
      desc << "Timebase: " << (fast ? SEQ_PPQN : SEQ_PPQN / 2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Timebase", desc.str().c_str(), CLR_TEMPO, ICON_TEMPO);

      if (parentSeq->fastTempo != fast) {
        addTempoBPMNoItem(parentSeq->getTempoInBPM(parentSeq->spcTempo, fast));
        parentSeq->fastTempo = fast;
      }
      break;
    }

    default: {
      auto descr = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }

  //assert(curOffset >= dwOffset);

  //ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  // increase delta-time
  //if (eventType == EVENT_NOTE || eventType == EVENT_NOTE_WITH_PARAM) { // rest is
  //	AddTime(spcDeltaTime);
  //}

  return bContinue;
}

void MoriSnesTrack::parseInstrument(uint16_t instrAddress, uint8_t instrNum) {
  MoriSnesSeq *parentSeq = (MoriSnesSeq *) this->parentSeq;

  uint16_t curOffset = instrAddress;

  bool percussion = ((readByte(curOffset++) & 1) != 0);
  parentSeq->InstrumentHints[instrAddress].percussion = percussion;

  if (!percussion) {
    parseInstrumentEvents(curOffset, instrNum);
  }
  else {
    int16_t instrOffset;
    uint16_t instrPtrAddressMax = 0xffff;
    uint8_t percNoteKey = 0;
    while (curOffset < instrPtrAddressMax) {
      instrOffset = readShort(curOffset);
      curOffset += 2;
      uint16_t percInstrAddress = curOffset + instrOffset;
      if (percInstrAddress < instrPtrAddressMax) {
        instrPtrAddressMax = percInstrAddress;
      }

      parseInstrumentEvents(percInstrAddress, instrNum, percussion, percNoteKey);
      percNoteKey++;
    }
  }
}

void MoriSnesTrack::parseInstrumentEvents(uint16_t offset, uint8_t instrNum, bool percussion, uint8_t percNoteKey) {
  MoriSnesSeq *parentSeq = (MoriSnesSeq *) this->parentSeq;
  uint16_t instrAddress = parentSeq->InstrumentAddresses[instrNum];

  MoriSnesInstrHint *instrHint;
  if (!percussion) {
    instrHint = &parentSeq->InstrumentHints[instrAddress].instrHint;
  }
  else {
    parentSeq->InstrumentHints[instrAddress].percHints.resize(percNoteKey + 1);
    instrHint = &parentSeq->InstrumentHints[instrAddress].percHints[percNoteKey];
  }

  bool bContinue = true;
  uint32_t curOffset = offset;
  uint32_t seqStartAddress = curOffset;
  uint32_t seqEndAddress = curOffset;

  // uint8_t instrDeltaTime = 0;
  uint8_t instrCallStackPtr = 0;
  uint8_t instrCallStack[MORISNES_CALLSTACK_SIZE];

  while (bContinue) {
    if (curOffset >= 0xffff) {
      break;
    }

    if (curOffset < seqStartAddress) {
      seqStartAddress = curOffset;
    }

    uint8_t statusByte = readByte(curOffset++);

    // uint8_t newDelta = instrDeltaTime;
    if (statusByte < 0x80) {
      // newDelta = statusByte;
      // if (newDelta != 0) {
        // instrDeltaTime = newDelta;
      // }

      statusByte = readByte(curOffset);
      if (statusByte < 0x80) {
        // duration
        curOffset++;

        statusByte = readByte(curOffset);
        if (statusByte < 0x80) {
          // velocity
          curOffset++;
        }
      }

      if (curOffset >= 0xffff) {
        break;
      }

      statusByte = readByte(curOffset++);

      // workaround: handle statusByte < 0x80 in some sequences (64 64 64 64 ...)
      // example: Lennus 2 - Title ($74f5)
      // example: Shin SD Sengokuden: Daishougun R - Dark Army Corps ($c9be)
      if (statusByte < 0x80) {
        statusByte = 0x80 | (statusByte & 0x1f);
      }
    }

    MoriSnesSeqEventType eventType = (MoriSnesSeqEventType) 0;
    std::map<uint8_t, MoriSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
    if (pEventType != parentSeq->EventMap.end()) {
      eventType = pEventType->second;
    }

    switch (eventType) {
      case EVENT_UNKNOWN0: {
        break;
      }

      case EVENT_UNKNOWN1: {
        curOffset++;
        break;
      }

      case EVENT_UNKNOWN2: {
        curOffset += 2;
        break;
      }

      case EVENT_UNKNOWN3: {
        curOffset += 3;
        break;
      }

      case EVENT_UNKNOWN4: {
        curOffset += 4;
        break;
      }

      case EVENT_NOTE:
      case EVENT_NOTE_WITH_PARAM: {
        if (eventType == EVENT_NOTE_WITH_PARAM) {
          curOffset++;
        }
        break;
      }

      case EVENT_PAN: {
        int8_t newPan = readByte(curOffset++);
        if (newPan > 32) {
          newPan = 32;
        }
        instrHint->pan = newPan;
        break;
      }

      case EVENT_VOLUME:
        curOffset++;
        break;

      case EVENT_TUNING:
        curOffset++;
        break;

      case EVENT_GOTO: {
        uint16_t dest = readShort(curOffset);
        curOffset += 2;
        dest += curOffset; // relative offset to address

        if (dest > curOffset) {
          // Gokinjo Bouken Tai - Town ($1581)
          curOffset = dest;
        }
        else {
          // prevent infinite loop
          bContinue = false;
        }

        break;
      }

      case EVENT_CALL: {
        uint16_t dest = readShort(curOffset);
        curOffset += 2;
        dest += curOffset; // relative offset to address

        if (instrCallStackPtr + 2 > MORISNES_CALLSTACK_SIZE) {
          // stack overflow
          bContinue = false;
          break;
        }

        // save return address
        instrCallStack[instrCallStackPtr++] = curOffset & 0xff;
        instrCallStack[instrCallStackPtr++] = (curOffset >> 8) & 0xff;

        curOffset = dest;
        break;
      }

      case EVENT_RET: {
        if (instrCallStackPtr < 2) {
          // access violation
          bContinue = false;
          break;
        }

        curOffset = instrCallStack[instrCallStackPtr - 2] | (instrCallStack[instrCallStackPtr - 1] << 8);
        instrCallStackPtr -= 2;
        break;
      }

      case EVENT_LOOP_START: {
        uint8_t count = readByte(curOffset++);

        if (instrCallStackPtr + 3 > MORISNES_CALLSTACK_SIZE) {
          // stack overflow
          bContinue = false;
          break;
        }

        // save loop start address and repeat count
        instrCallStack[instrCallStackPtr++] = curOffset & 0xff;
        instrCallStack[instrCallStackPtr++] = (curOffset >> 8) & 0xff;
        instrCallStack[instrCallStackPtr++] = count;

        break;
      }

      case EVENT_LOOP_END: {
        if (instrCallStackPtr < 3) {
          // access violation
          bContinue = false;
          break;
        }

        uint8_t count = instrCallStack[instrCallStackPtr - 1];
        if (--count == 0) {
          // repeat end, fall through
          instrCallStackPtr -= 3;
        }
        else {
          // repeat again
          instrCallStack[instrCallStackPtr - 1] = count;
          curOffset = instrCallStack[instrCallStackPtr - 3] | (instrCallStack[instrCallStackPtr - 2] << 8);
        }

        break;
      }

      case EVENT_END:
        bContinue = false;
        break;

      case EVENT_TRANSPOSE:
        instrHint->transpose = readByte(curOffset++);
        break;

      case EVENT_TRANSPOSE_REL: {
        int8_t delta = readByte(curOffset++);
        instrHint->transpose += delta;
        break;
      }

      case EVENT_TUNING_REL:
        curOffset++;
        break;

      case EVENT_KEY_ON:
        break;

      case EVENT_KEY_OFF:
        break;

      case EVENT_VOLUME_REL:
        curOffset++;
        break;

      case EVENT_INSTR: {
        int16_t rgnOffset = readShort(curOffset);
        curOffset += 2;
        uint16_t rgnAddress = curOffset + rgnOffset;
        instrHint->rgnAddress = rgnAddress;
        break;
      }

      default:
//#ifdef _WIN32
//			std::ostringstream ssTrace;
//			ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
//			OutputDebugString(ssTrace.str().c_str());
//#endif

        bContinue = false;
        break;
    }

    if (curOffset > seqEndAddress) {
      seqEndAddress = curOffset;
    }
  }

  instrHint->seqAddress = seqStartAddress;
  instrHint->seqSize = seqEndAddress - seqStartAddress;

  instrHint->startAddress = instrHint->seqAddress;
  instrHint->size = instrHint->seqSize;

  if (instrHint->rgnAddress != 0) {
    if (instrHint->rgnAddress < instrHint->startAddress) {
      instrHint->startAddress = instrHint->rgnAddress;
    }
    if (instrHint->rgnAddress + 7 > instrHint->startAddress + instrHint->size) {
      instrHint->size = (instrHint->rgnAddress + 7) - instrHint->startAddress;
    }
  }
}
