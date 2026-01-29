#include "PrismSnesSeq.h"
#include "ScaleConversion.h"
#include <spdlog/fmt/fmt.h>

// TODO: Fix envelope event length

DECLARE_FORMAT(PrismSnes);

static constexpr int MAX_TRACKS = 24;
static constexpr uint16_t SEQ_PPQN = 48;
static constexpr uint8_t NOTE_VELOCITY = 100;

//  ************
//  PrismSnesSeq
//  ************

const uint8_t PrismSnesSeq::PAN_TABLE_1[21] = {
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
    0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
    0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
};

const uint8_t PrismSnesSeq::PAN_TABLE_2[21] = {
    0x1e, 0x28, 0x32, 0x3c, 0x46, 0x50, 0x5a, 0x64,
    0x6e, 0x78, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
};

PrismSnesSeq::PrismSnesSeq(RawFile *file, PrismSnesVersion ver, uint32_t seqdataOffset, std::string newName)
    : VGMSeq(PrismSnesFormat::name, file, seqdataOffset, 0, newName), version(ver),
      envContainer(NULL) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);
  setAlwaysWriteInitialTempo(getTempoInBPM(0x82));

  loadEventMap();
}

PrismSnesSeq::~PrismSnesSeq() {
}

void PrismSnesSeq::demandEnvelopeContainer(uint32_t offset) {
  if (envContainer == NULL) {
    envContainer = addHeader(offset, 0, "Envelopes");
  }

  if (offset < envContainer->offset()) {
    if (envContainer->length() != 0) {
      envContainer->setLength(envContainer->length() + (envContainer->offset() - offset));
    }
    envContainer->setOffset(offset);
  }
}

void PrismSnesSeq::resetVars() {
  VGMSeq::resetVars();

  conditionSwitch = false;
}

bool PrismSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(offset(), 0);

  uint32_t curOffset = offset();
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS + 1; trackIndex++) {
    if (curOffset + 1 > 0x10000) {
      return false;
    }

    uint8_t channel = readByte(curOffset);
    if (channel >= 0x80) {
      header->addChild(curOffset, 1, "Header End");
      break;
    }
    if (trackIndex >= MAX_TRACKS) {
      return false;
    }

    auto trackName = fmt::format("Track {}", trackIndex + 1);
    VGMHeader *trackHeader = header->addHeader(curOffset, 4, trackName);

    trackHeader->addChild(curOffset, 1, "Logical Channel");
    curOffset++;

    uint8_t a01 = readByte(curOffset);
    trackHeader->addChild(curOffset, 1, "Physical Channel + Flags");
    curOffset++;

    uint16_t addrTrackStart = readShort(curOffset);
    trackHeader->addChild(curOffset, 2, "Track Pointer");
    curOffset += 2;

    PrismSnesTrack *track = new PrismSnesTrack(this, addrTrackStart);
    aTracks.push_back(track);
  }

  return true;
}

bool PrismSnesSeq::parseTrackPointers(void) {
  return true;
}

void PrismSnesSeq::loadEventMap() {
  int statusByte;

  for (statusByte = 0x00; statusByte <= 0x7f; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  for (statusByte = 0x80; statusByte <= 0x9f; statusByte++) {
    EventMap[statusByte] = EVENT_NOISE_NOTE;
  }

  EventMap[0xc0] = EVENT_TEMPO;
  EventMap[0xc1] = EVENT_TEMPO;
  EventMap[0xc2] = EVENT_TEMPO;
  EventMap[0xc3] = EVENT_TEMPO;
  EventMap[0xc4] = EVENT_TEMPO;
  EventMap[0xc5] = EVENT_CONDITIONAL_JUMP;
  EventMap[0xc6] = EVENT_CONDITION;
  EventMap[0xc7] = EVENT_UNKNOWN1;
  EventMap[0xc8] = EVENT_UNKNOWN2;
  EventMap[0xc9] = EVENT_UNKNOWN2;
  EventMap[0xca] = EVENT_RESTORE_ECHO_PARAM;
  EventMap[0xcb] = EVENT_SAVE_ECHO_PARAM;
  EventMap[0xcc] = EVENT_UNKNOWN1;
  EventMap[0xcd] = EVENT_SLUR_OFF;
  EventMap[0xce] = EVENT_SLUR_ON;
  EventMap[0xcf] = EVENT_VOLUME_ENVELOPE;
  EventMap[0xd0] = EVENT_DEFAULT_PAN_TABLE_1;
  EventMap[0xd1] = EVENT_DEFAULT_PAN_TABLE_2;
  EventMap[0xd2] = EVENT_UNKNOWN0;
  EventMap[0xd3] = EVENT_INC_APU_PORT_3;
  EventMap[0xd4] = EVENT_INC_APU_PORT_2;
  EventMap[0xd5] = EVENT_PLAY_SONG_3;
  EventMap[0xd6] = EVENT_PLAY_SONG_2;
  EventMap[0xd7] = EVENT_PLAY_SONG_1;
  EventMap[0xd8] = EVENT_TRANSPOSE_REL;
  EventMap[0xd9] = EVENT_PAN_ENVELOPE;
  EventMap[0xda] = EVENT_PAN_TABLE;
  EventMap[0xdb] = EVENT_NOP2;
  EventMap[0xdc] = EVENT_DEFAULT_LENGTH_OFF;
  EventMap[0xdd] = EVENT_DEFAULT_LENGTH;
  EventMap[0xde] = EVENT_LOOP_UNTIL;
  EventMap[0xdf] = EVENT_LOOP_UNTIL_ALT;
  EventMap[0xe0] = EVENT_RET;
  EventMap[0xe1] = EVENT_CALL;
  EventMap[0xe2] = EVENT_GOTO;
  EventMap[0xe3] = EVENT_TRANSPOSE;
  EventMap[0xe4] = EVENT_TUNING;
  EventMap[0xe5] = EVENT_VIBRATO_DELAY;
  EventMap[0xe6] = EVENT_VIBRATO_OFF;
  EventMap[0xe7] = EVENT_VIBRATO;
  EventMap[0xe8] = EVENT_UNKNOWN1;
  EventMap[0xe9] = EVENT_PITCH_SLIDE;
  EventMap[0xea] = EVENT_VOLUME_REL;
  EventMap[0xeb] = EVENT_PAN;
  EventMap[0xec] = EVENT_VOLUME;
  EventMap[0xed] = EVENT_UNKNOWN_EVENT_ED;
  EventMap[0xee] = EVENT_REST;
  EventMap[0xef] = EVENT_GAIN_ENVELOPE_REST;
  EventMap[0xf0] = EVENT_GAIN_ENVELOPE_DECAY_TIME;
  EventMap[0xf1] = EVENT_MANUAL_DURATION_OFF;
  EventMap[0xf2] = EVENT_MANUAL_DURATION_ON;
  EventMap[0xf3] = EVENT_AUTO_DURATION_THRESHOLD;
  EventMap[0xf4] = EVENT_TIE_WITH_DUR;
  EventMap[0xf5] = EVENT_TIE;
  EventMap[0xf6] = EVENT_GAIN_ENVELOPE_SUSTAIN;
  EventMap[0xf7] = EVENT_ECHO_VOLUME_ENVELOPE;
  EventMap[0xf8] = EVENT_ECHO_VOLUME;
  EventMap[0xf9] = EVENT_ECHO_OFF;
  EventMap[0xfa] = EVENT_ECHO_ON;
  EventMap[0xfb] = EVENT_ECHO_PARAM;
  EventMap[0xfc] = EVENT_ADSR;
  EventMap[0xfd] = EVENT_GAIN_ENVELOPE_DECAY;
  EventMap[0xfe] = EVENT_INSTRUMENT;
  EventMap[0xff] = EVENT_END;

  if (version == PRISMSNES_CGV) {
    EventMap[0xc0] = EventMap[0xd0];
    EventMap[0xc1] = EventMap[0xd0];
    EventMap[0xc2] = EventMap[0xd0];
    EventMap[0xc3] = EventMap[0xd0];
    EventMap[0xc4] = EventMap[0xd0];
    EventMap[0xc5] = EventMap[0xd0];
    EventMap[0xc6] = EventMap[0xd0];
    EventMap[0xc7] = EventMap[0xd0];
    EventMap[0xc8] = EventMap[0xd0];
    EventMap[0xc9] = EventMap[0xd0];
    EventMap[0xca] = EventMap[0xd0];
    EventMap[0xcb] = EventMap[0xd0];
    EventMap[0xcc] = EventMap[0xd0];
    EventMap[0xcd] = EventMap[0xd0];
    EventMap[0xce] = EventMap[0xd0];
    EventMap[0xcf] = EventMap[0xd0];
    EventMap[0xdb] = EVENT_UNKNOWN2;
  }
  else if (version == PRISMSNES_DO) {
    EventMap[0xc0] = EventMap[0xc5];
    EventMap[0xc1] = EventMap[0xc5];
    EventMap[0xc2] = EventMap[0xc5];
    EventMap[0xc3] = EventMap[0xc5];
    EventMap[0xc4] = EventMap[0xc5];
  }
}

double PrismSnesSeq::getTempoInBPM(uint8_t tempo) {
  if (tempo != 0) {
    return 60000000.0 / (SEQ_PPQN * (125 * tempo));
  }
  else {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}

//  **************
//  PrismSnesTrack
//  **************

PrismSnesTrack::PrismSnesTrack(PrismSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void PrismSnesTrack::resetVars() {
  SeqTrack::resetVars();

  panTable.assign(std::begin(PrismSnesSeq::PAN_TABLE_1), std::end(PrismSnesSeq::PAN_TABLE_1));

  defaultLength = 0;
  slur = false;
  manualDuration = false;
  prevNoteSlurred = false;
  prevNoteKey = -1;
  spcVolume = 0;
  loopCount = 0;
  loopCountAlt = 0;
  subReturnAddr = 0;
}

bool PrismSnesTrack::readEvent() {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  PrismSnesSeqEventType eventType = (PrismSnesSeqEventType) 0;
  std::map<uint8_t, PrismSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

    case EVENT_NOP2: {
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", "", Type::Nop);
      break;
    }

    case EVENT_NOTE:
    case EVENT_NOISE_NOTE: {
      uint8_t key = statusByte & 0x7f;

      uint8_t len;
      if (!readDeltaTime(curOffset, len)) {
        return false;
      }

      uint8_t durDelta;
      if (!readDuration(curOffset, len, durDelta)) {
        return false;
      }

      uint8_t dur = getDuration(curOffset, len, durDelta);

      if (slur) {
        prevNoteSlurred = true;
      }

      if (prevNoteSlurred && key == prevNoteKey) {
        makePrevDurNoteEnd(getTime() + dur);
        desc = fmt::format("Abs Key: {} ({})  Velocity: {}  Duration: {}", key,
                           MidiEvent::getNoteName(key), NOTE_VELOCITY, dur);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Note (Tied)", desc, Type::DurationNote);
      }
      else {
        if (eventType == EVENT_NOISE_NOTE) {
          addNoteByDur(beginOffset, curOffset - beginOffset, key, NOTE_VELOCITY, dur, "Noise Note");
        }
        else {
          addNoteByDur(beginOffset, curOffset - beginOffset, key, NOTE_VELOCITY, dur, "Note");
        }
      }
      addTime(len);

      prevNoteKey = key;
      prevNoteSlurred = false;
      break;
    }

    case EVENT_PITCH_SLIDE: {
      uint8_t noteFrom = readByte(curOffset++);
      uint8_t noteTo = readByte(curOffset++);

      uint8_t noteNumberFrom = noteFrom & 0x7f;
      uint8_t noteNumberTo = noteTo & 0x7f;

      uint8_t len;
      if (!readDeltaTime(curOffset, len)) {
        return false;
      }

      uint8_t durDelta;
      if (!readDuration(curOffset, len, durDelta)) {
        return false;
      }

      uint8_t dur = getDuration(curOffset, len, durDelta);

      auto nameFrom = ((noteFrom & 0x80) != 0 ? "Noise" : MidiEvent::getNoteName(noteNumberFrom));
      auto nameTo = ((noteTo & 0x80) != 0 ? "Noise" : MidiEvent::getNoteName(noteNumberTo));
      desc = fmt::format(
          "Note Number (From): {:d} ({})  Note Number (To): {:d} ({})  Length: {:d}  Duration: {:d}",
          noteNumberFrom, nameFrom, noteNumberTo, nameTo, len, dur);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide", desc, Type::PitchBendSlide);

      addNoteByDurNoItem(noteNumberFrom, NOTE_VELOCITY, dur);
      addTime(len);
      break;
    }

    case EVENT_TIE_WITH_DUR: {
      uint8_t len;
      if (!readDeltaTime(curOffset, len)) {
        return false;
      }

      uint8_t durDelta;
      if (!readDuration(curOffset, len, durDelta)) {
        return false;
      }

      uint8_t dur = getDuration(curOffset, len, durDelta);

      desc = fmt::format("Length: {:d}  Duration: {:d}", len, dur);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tie with Duration", desc, Type::Tie);
      makePrevDurNoteEnd(getTime() + dur);
      addTime(len);
      break;
    }

    case EVENT_TIE: {
      prevNoteSlurred = true;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", "", Type::Tie);
      break;
    }

    case EVENT_UNKNOWN_EVENT_ED: {
      uint8_t arg1 = readByte(curOffset++);
      uint16_t arg2 = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Arg1: {:d}  Arg2: ${:04X}", arg1, arg2);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_REST: {
      uint8_t len;
      if (!readDeltaTime(curOffset, len)) {
        return false;
      }

      desc = fmt::format("Duration: {:d}", len);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Rest", desc, Type::Rest);
      addTime(len);
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = readByte(curOffset++);
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM(newTempo));
      break;
    }

    case EVENT_CONDITIONAL_JUMP: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Conditional Jump", desc, Type::Loop);

      if (parentSeq->conditionSwitch) {
        curOffset = dest;
      }
      break;
    }

    case EVENT_CONDITION: {
      parentSeq->conditionSwitch = true;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Condition On", "", Type::ChangeState);
      break;
    }

    case EVENT_RESTORE_ECHO_PARAM: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Restore Echo Param", "", Type::Reverb);
      break;
    }

    case EVENT_SAVE_ECHO_PARAM: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Save Echo Param", "", Type::Reverb);
      break;
    }

    case EVENT_SLUR_OFF: {
      slur = false;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Slur Off", "", Type::Portamento);
      break;
    }

    case EVENT_SLUR_ON: {
      slur = true;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Slur On", "", Type::Portamento);
      break;
    }

    case EVENT_VOLUME_ENVELOPE: {
      uint16_t envelopeAddress = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Envelope: ${:04X}", envelopeAddress);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume Envelope", desc, Type::VolumeEnvelope);
      addVolumeEnvelope(envelopeAddress);
      break;
    }

    case EVENT_DEFAULT_PAN_TABLE_1: {
      panTable.assign(std::begin(PrismSnesSeq::PAN_TABLE_1), std::end(PrismSnesSeq::PAN_TABLE_1));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Default Pan Table #1", "", Type::Control);
      break;
    }

    case EVENT_DEFAULT_PAN_TABLE_2: {
      panTable.assign(std::begin(PrismSnesSeq::PAN_TABLE_2), std::end(PrismSnesSeq::PAN_TABLE_2));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Default Pan Table #2", "", Type::Control);
      break;
    }

    case EVENT_INC_APU_PORT_3: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Increment APU Port 3", "", Type::ChangeState);
      break;
    }

    case EVENT_INC_APU_PORT_2: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Increment APU Port 2", "", Type::ChangeState);
      break;
    }

    case EVENT_PLAY_SONG_3: {
      uint8_t songIndex = readByte(curOffset++);
      desc = fmt::format("Song Index: {:d}", songIndex);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Play Song (3)", desc, Type::ChangeState);
      break;
    }

    case EVENT_PLAY_SONG_2: {
      uint8_t songIndex = readByte(curOffset++);
      desc = fmt::format("Song Index: {:d}", songIndex);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Play Song (2)", desc, Type::ChangeState);
      break;
    }

    case EVENT_PLAY_SONG_1: {
      uint8_t songIndex = readByte(curOffset++);
      desc = fmt::format("Song Index: {:d}", songIndex);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Play Song (1)", desc, Type::ChangeState);
      break;
    }

    case EVENT_TRANSPOSE_REL: {
      int8_t delta = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, transpose + delta, "Transpose (Relative)");
      break;
    }

    case EVENT_PAN_ENVELOPE: {
      uint16_t envelopeAddress = readShort(curOffset);
      curOffset += 2;
      uint16_t envelopeSpeed = readByte(curOffset++);
      desc = fmt::format("Envelope: ${:04X}  Speed: {:d}", envelopeAddress, envelopeSpeed);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan Envelope", desc, Type::PanEnvelope);
      addPanEnvelope(envelopeAddress);
      break;
    }

    case EVENT_PAN_TABLE: {
      uint16_t panTableAddress = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Pan Table: ${:04X}", panTableAddress);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan Table", desc, Type::Control);

      // update pan table
      if (panTableAddress + 21 <= 0x10000) {
        uint8_t newPanTable[21];
        readBytes(panTableAddress, 21, newPanTable);
        panTable.assign(std::begin(newPanTable), std::end(newPanTable));
      }

      addPanTable(panTableAddress);
      break;
    }

    case EVENT_DEFAULT_LENGTH_OFF: {
      defaultLength = 0;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Default Length Off", "", Type::DurationNote);
      break;
    }

    case EVENT_DEFAULT_LENGTH: {
      defaultLength = readByte(curOffset++);
      desc = fmt::format("Duration: {:d}", defaultLength);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Default Length", desc, Type::DurationNote);
      break;
    }

    case EVENT_LOOP_UNTIL: {
      uint8_t count = readByte(curOffset++);
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Loop Count: {:d}  Destination: ${:04X}", count, dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Until", desc, Type::RepeatEnd);

      bool doJump;
      if (loopCount == 0) {
        loopCount = count;
        doJump = true;
      }
      else {
        loopCount--;
        if (loopCount != 0) {
          doJump = true;
        }
        else {
          doJump = false;
        }
      }

      if (doJump) {
        curOffset = dest;
      }

      break;
    }

    case EVENT_LOOP_UNTIL_ALT: {
      uint8_t count = readByte(curOffset++);
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Loop Count: {:d}  Destination: ${:04X}", count, dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Until (Alt)", desc, Type::RepeatEnd);

      bool doJump;
      if (loopCountAlt == 0) {
        loopCountAlt = count;
        doJump = true;
      }
      else {
        loopCountAlt--;
        if (loopCountAlt != 0) {
          doJump = true;
        }
        else {
          doJump = false;
        }
      }

      if (doJump) {
        curOffset = dest;
      }

      break;
    }

    case EVENT_RET: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern End", "", Type::RepeatEnd);

      if (subReturnAddr != 0) {
        curOffset = subReturnAddr;
        subReturnAddr = 0;
      }

      break;
    }

    case EVENT_CALL: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern Play", desc, Type::RepeatStart);

      subReturnAddr = curOffset;
      curOffset = dest;

      break;
    }

    case EVENT_GOTO: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      uint32_t length = curOffset - beginOffset;

      if (curOffset < 0x10000 && readByte(curOffset) == 0xff) {
        addGenericEvent(curOffset, 1, "End of Track", "", Type::TrackEnd);
      }

      curOffset = dest;
      if (!isOffsetUsed(dest)) {
        addGenericEvent(beginOffset, length, "Jump", desc, Type::LoopForever);
      }
      else {
        bContinue = addLoopForever(beginOffset, length, "Jump");
      }

      if (curOffset < offset()) {
        setOffset(curOffset);
      }

      break;
    }

    case EVENT_TRANSPOSE: {
      int8_t newTranspose = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, newTranspose);
      break;
    }

    case EVENT_TUNING: {
      uint8_t newTuning = readByte(curOffset++);
      double semitones = newTuning / 256.0;
      addFineTuning(beginOffset, curOffset - beginOffset, semitones * 100.0);
      break;
    }

    case EVENT_VIBRATO_DELAY: {
      uint8_t lfoDelay = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}", lfoDelay);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Delay", desc, Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Off", "", Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO: {
      uint8_t lfoDelay = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Arg2: {:d}  Arg3: {:d}", lfoDelay, arg2, arg3);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Vibrato);
      break;
    }

    case EVENT_VOLUME_REL: {
      int8_t delta = readByte(curOffset++);
      if (spcVolume + delta > 255) {
        spcVolume = 255;
      }
      else if (spcVolume + delta < 0) {
        spcVolume = 0;
      }
      else {
        spcVolume += delta;
      }
      addVol(beginOffset, curOffset - beginOffset, spcVolume / 2, "Volume (Relative)");
      break;
    }

    case EVENT_PAN: {
      uint8_t newPan = readByte(curOffset++);
      if (newPan > 20) {
        // unexpected value
        newPan = 20;
      }

      // TODO: use correct pan table
      double volumeLeft;
      double volumeRight;
      // actual engine divides pan by 256, though pan value must be always 7-bit, perhaps
      volumeLeft = panTable[20 - newPan] / 128.0;
      volumeRight = panTable[newPan] / 128.0;

      // TODO: fix volume scale when L+R > 1.0?
      double volumeScale;
      int8_t midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &volumeScale);
      volumeScale = std::min(volumeScale, 1.0); // workaround

      addPan(beginOffset, curOffset - beginOffset, midiPan);
      addExpressionNoItem(std::round(127.0 * volumeScale));
      break;
    }

    case EVENT_VOLUME: {
      uint8_t newVolume = readByte(curOffset++);
      spcVolume = newVolume;
      addVol(beginOffset, curOffset - beginOffset, spcVolume / 2);
      break;
    }

    case EVENT_GAIN_ENVELOPE_REST: {
      uint16_t envelopeAddress = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Envelope: ${:04X}", envelopeAddress);
      addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN Envelope (Rest)", desc, Type::Adsr);
      addGAINEnvelope(envelopeAddress);
      break;
    }

    case EVENT_GAIN_ENVELOPE_DECAY_TIME: {
      uint8_t dur = readByte(curOffset++);
      uint8_t gain = readByte(curOffset++);
      desc = fmt::format("Duration: Full-Length - {:d}  GAIN: ${:02X}", dur, gain);
      addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN Envelope Decay Time", desc, Type::Adsr);
      break;
    }

    case EVENT_MANUAL_DURATION_OFF: {
      manualDuration = false;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Manual Duration Off", "", Type::DurationNote);
      break;
    }

    case EVENT_MANUAL_DURATION_ON: {
      manualDuration = true;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Manual Duration On", "", Type::DurationNote);
      break;
    }

    case EVENT_AUTO_DURATION_THRESHOLD: {
      manualDuration = false;
      autoDurationThreshold = readByte(curOffset++);
      desc = fmt::format("Duration: Full-Length - {:d}", autoDurationThreshold);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Auto Duration Threshold", desc, Type::DurationNote);
      break;
    }

    case EVENT_GAIN_ENVELOPE_SUSTAIN: {
      uint16_t envelopeAddress = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Envelope: ${:04X}", envelopeAddress);
      addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN Envelope (Sustain)", desc, Type::Adsr);
      addGAINEnvelope(envelopeAddress);
      break;
    }

    case EVENT_ECHO_VOLUME_ENVELOPE: {
      uint16_t envelopeAddress = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Envelope: ${:04X}", envelopeAddress);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume Envelope", desc, Type::Reverb);
      addEchoVolumeEnvelope(envelopeAddress);
      break;
    }

    case EVENT_ECHO_VOLUME: {
      int8_t echoVolumeLeft = readByte(curOffset++);
      int8_t echoVolumeRight = readByte(curOffset++);
      int8_t echoVolumeMono = readByte(curOffset++);
      desc = fmt::format("Left Volume: {:d}  Right Volume: {:d}  Mono Volume: {:d}",
                         echoVolumeLeft, echoVolumeRight, echoVolumeMono);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume", desc, Type::Reverb);
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

    case EVENT_ECHO_PARAM: {
      int8_t echoFeedback = readByte(curOffset++);
      int8_t echoVolumeLeft = readByte(curOffset++);
      int8_t echoVolumeRight = readByte(curOffset++);
      int8_t echoVolumeMono = readByte(curOffset++);
      desc = fmt::format("Feedback: {:d}  Left Volume: {:d}  Right Volume: {:d}  Mono Volume: {:d}",
                         echoFeedback, echoVolumeLeft, echoVolumeRight, echoVolumeMono);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Param", desc, Type::Reverb);
      break;
    }

    case EVENT_ADSR: {
      uint8_t param = readByte(curOffset);
      if (param >= 0x80) {
        uint8_t adsr1 = readByte(curOffset++);
        uint8_t adsr2 = readByte(curOffset++);
        desc = fmt::format("ADSR(1): ${:02X}  ADSR(2): ${:02X}", adsr1, adsr2);
      } else {
        uint8_t instrNum = readByte(curOffset++);
        desc = fmt::format("Instrument: {:d}", instrNum);
      }
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      break;
    }

    case EVENT_GAIN_ENVELOPE_DECAY: {
      uint16_t envelopeAddress = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Envelope: ${:04X}", envelopeAddress);
      addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN Envelope (Decay)", desc, Type::Adsr);
      addGAINEnvelope(envelopeAddress);
      break;
    }

    case EVENT_INSTRUMENT: {
      uint8_t instrNum = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, instrNum, true);
      break;
    }

    case EVENT_END: {
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;
    }

    default: {
      auto descr = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }

  //assert(curOffset >= parentSeq->offset());

  //std::ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}

bool PrismSnesTrack::readDeltaTime(uint32_t &curOffset, uint8_t &len) {
  if (curOffset + 1 > 0x10000) {
    return false;
  }

  if (defaultLength != 0) {
    len = defaultLength;
  }
  else {
    len = readByte(curOffset++);
  }
  return true;
}

bool PrismSnesTrack::readDuration(uint32_t &curOffset, uint8_t len, uint8_t &durDelta) {
  if (curOffset + 1 > 0x10000) {
    return false;
  }

  if (manualDuration) {
    durDelta = readByte(curOffset++);
  }
  else {
    durDelta = len >> 1;
    if (durDelta > autoDurationThreshold) {
      durDelta = autoDurationThreshold;
    }
  }

  if (durDelta > len) {
    // unexpected value
    durDelta = 0;
  }

  return true;
}

uint8_t PrismSnesTrack::getDuration(uint32_t curOffset, uint8_t len, uint8_t durDelta) {
  // TODO: adjust duration for slur/tie
  if (curOffset + 1 <= 0x10000) {
    uint8_t nextStatusByte = readByte(curOffset);
    if (nextStatusByte == 0xee || nextStatusByte == 0xf4 || nextStatusByte == 0xce || nextStatusByte == 0xf5) {
      durDelta = 0;
    }
  }

  if (durDelta > len) {
    // unexpected value
    durDelta = 0;
  }

  return len - durDelta;
}

void PrismSnesTrack::addVolumeEnvelope(uint16_t envelopeAddress) {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;
  parentSeq->demandEnvelopeContainer(envelopeAddress);

  if (!isOffsetUsed(envelopeAddress)) {
    auto envelopeName = fmt::format("Volume Envelope (${:04X})", envelopeAddress);
    VGMHeader *envHeader = parentSeq->envContainer->addHeader(envelopeAddress, 0, envelopeName);

    uint16_t envOffset = 0;
    while (envelopeAddress + envOffset + 2 <= 0x10000) {
      std::string eventName;

      uint8_t volumeFrom = readByte(envelopeAddress + envOffset);
      int8_t volumeDelta = readByte(envelopeAddress + envOffset + 1);
      if (volumeFrom == 0 && volumeDelta == 0) {
        // $00 $00 $xx $yy sets offset to $yyxx
        uint16_t newAddress = readByte(envelopeAddress + envOffset + 2);

        eventName = fmt::format("Jump to ${:04X}", newAddress);
        envHeader->addChild(envelopeAddress + envOffset, 4, eventName);
        envOffset += 4;
        break;
      }

      uint8_t envelopeSpeed = readByte(envelopeAddress + envOffset + 2);
      uint8_t deltaTime = readByte(envelopeAddress + envOffset + 3);
      eventName = fmt::format("Volume: {:d}  Volume Delta: {:d}  Envelope Speed: {:d}  Delta-Time: {:d}",
                              volumeFrom, volumeDelta, envelopeSpeed, deltaTime);
      envHeader->addChild(envelopeAddress + envOffset, 4, eventName);
      envOffset += 4;

      // workaround: quit to prevent out of bound
      break;
    }

    envHeader->setLength(envOffset);
  }
}

void PrismSnesTrack::addPanEnvelope(uint16_t envelopeAddress) {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;
  parentSeq->demandEnvelopeContainer(envelopeAddress);

  if (!isOffsetUsed(envelopeAddress)) {
    auto envelopeName = fmt::format("Pan Envelope (${:04X})", envelopeAddress);
    VGMHeader *envHeader = parentSeq->envContainer->addHeader(envelopeAddress, 0, envelopeName);

    uint16_t envOffset = 0;
    while (envOffset < 0x100) {
      std::string eventName;

      uint8_t newPan = readByte(envelopeAddress + envOffset);
      if (newPan >= 0x80) {
        // $ff $xx sets offset to $xx
        uint8_t newOffset = readByte(envelopeAddress + envOffset + 1);

        eventName = fmt::format("Jump to ${:04X}", envelopeAddress + newOffset);
        envHeader->addChild(envelopeAddress + envOffset, 2, eventName);
        envOffset += 2;
        break;
      }

      eventName = fmt::format("Pan: {:d}", newPan);
      envHeader->addChild(envelopeAddress + envOffset, 1, eventName);
      envOffset++;

      // workaround: quit to prevent out of bound
      break;
    }

    envHeader->setLength(envOffset);
  }
}

void PrismSnesTrack::addEchoVolumeEnvelope(uint16_t envelopeAddress) {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;
  parentSeq->demandEnvelopeContainer(envelopeAddress);

  if (!isOffsetUsed(envelopeAddress)) {
    auto envelopeName = fmt::format("Echo Volume Envelope (${:04X})", envelopeAddress);
    VGMHeader *envHeader = parentSeq->envContainer->addHeader(envelopeAddress, 0, envelopeName);

    uint16_t envOffset = 0;
    while (envOffset < 0x100) {
      std::string eventName;

      uint8_t deltaTime = readByte(envelopeAddress + envOffset);
      if (deltaTime == 0xff) {
        // $ff $xx sets offset to $xx
        uint8_t newOffset = readByte(envelopeAddress + envOffset + 1);

        eventName = fmt::format("Jump to ${:04X}", envelopeAddress + newOffset);
        envHeader->addChild(envelopeAddress + envOffset, 2, eventName);
        envOffset += 2;
        break;
      }

      int8_t echoVolumeLeft = readByte(envelopeAddress + envOffset + 1);
      int8_t echoVolumeRight = readByte(envelopeAddress + envOffset + 2);
      int8_t echoVolumeMono = readByte(envelopeAddress + envOffset + 3);
      eventName = fmt::format(
          "Delta-Time: {:d}  Left Volume: {:d}  Right Volume: {:d}  Mono Volume: {:d}",
          deltaTime, echoVolumeLeft, echoVolumeRight, echoVolumeMono);
      envHeader->addChild(envelopeAddress + envOffset, 4, eventName);
      envOffset += 4;

      // workaround: quit to prevent out of bound
      break;
    }

    envHeader->setLength(envOffset);
  }
}

void PrismSnesTrack::addGAINEnvelope(uint16_t envelopeAddress) {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;
  parentSeq->demandEnvelopeContainer(envelopeAddress);

  if (!isOffsetUsed(envelopeAddress)) {
    auto envelopeName = fmt::format("GAIN Envelope (${:04X})", envelopeAddress);
    VGMHeader *envHeader = parentSeq->envContainer->addHeader(envelopeAddress, 0, envelopeName);

    uint16_t envOffset = 0;
    while (envOffset < 0x100) {
      std::string eventName;

      uint8_t gain = readByte(envelopeAddress + envOffset);
      if (gain == 0xff) {
        // $ff $xx sets offset to $xx
        uint8_t newOffset = readByte(envelopeAddress + envOffset + 1);

        eventName = fmt::format("Jump to ${:04X}", envelopeAddress + newOffset);
        envHeader->addChild(envelopeAddress + envOffset, 2, eventName);
        envOffset += 2;
        break;
      }

      uint8_t deltaTime = readByte(envelopeAddress + envOffset + 1);
      eventName = fmt::format("GAIN: ${:02X}, Delta-Time: {:d}", gain, deltaTime);
      envHeader->addChild(envelopeAddress + envOffset, 2, eventName);
      envOffset += 2;

      // workaround: quit to prevent out of bound
      break;
    }

    envHeader->setLength(envOffset);
  }
}

void PrismSnesTrack::addPanTable(uint16_t panTableAddress) {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;
  parentSeq->demandEnvelopeContainer(panTableAddress);

  if (!isOffsetUsed(panTableAddress)) {
    auto eventName = fmt::format("Pan Table (${:04X})", panTableAddress);
    parentSeq->envContainer->addChild(panTableAddress, 21, eventName);
  }
}
