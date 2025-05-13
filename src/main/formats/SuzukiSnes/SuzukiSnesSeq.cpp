#include "SuzukiSnesSeq.h"

DECLARE_FORMAT(SuzukiSnes);

static constexpr int MAX_TRACKS = 8;
static constexpr uint16_t SEQ_PPQN = 48;
static constexpr uint8_t NOTE_VELOCITY = 100;

//  *************
//  SuzukiSnesSeq
//  *************

const uint8_t SuzukiSnesSeq::NOTE_DUR_TABLE[13] = {
    0xc0, 0x90, 0x60, 0x48, 0x30, 0x24, 0x20, 0x18,
    0x10, 0x0c, 0x08, 0x06, 0x03
};

SuzukiSnesSeq::SuzukiSnesSeq(RawFile *file, SuzukiSnesVersion ver, uint32_t seqdataOffset, std::string newName)
    : VGMSeq(SuzukiSnesFormat::name, file, seqdataOffset, 0, newName),
      version(ver) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

SuzukiSnesSeq::~SuzukiSnesSeq() {
}

void SuzukiSnesSeq::resetVars() {
  VGMSeq::resetVars();

  spcTempo = 0x81; // just in case
}

bool SuzukiSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(dwOffset, 0);
  uint32_t curOffset = dwOffset;

  // skip unknown stream
  if (version != SUZUKISNES_SD3) {
    while (true) {
      if (curOffset + 1 >= 0x10000) {
        return false;
      }

      uint8_t firstByte = readByte(curOffset);
      if (firstByte >= 0x80) {
        header->addChild(curOffset, 1, "Unknown Items End");
        curOffset++;
        break;
      }
      else {
        header->addUnknownChild(curOffset, 5);
        curOffset += 5;
      }
    }
  }

  // create tracks
  for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t addrTrackStart = readShort(curOffset);

    if (addrTrackStart != 0) {
      std::stringstream trackName;
      trackName << "Track Pointer " << (trackIndex + 1);
      header->addChild(curOffset, 2, trackName.str().c_str());

      aTracks.push_back(new SuzukiSnesTrack(this, addrTrackStart));
    }
    else {
      // example: Super Mario RPG - Where Am I Going?
      header->addChild(curOffset, 2, "NULL");
    }

    curOffset += 2;
  }

  header->setGuessedLength();

  return true;        //successful
}

bool SuzukiSnesSeq::parseTrackPointers() {
  return true;
}

void SuzukiSnesSeq::loadEventMap() {
  for (unsigned int statusByte = 0x00; statusByte <= 0xc3; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  EventMap[0xc4] = EVENT_OCTAVE_UP;
  EventMap[0xc5] = EVENT_OCTAVE_DOWN;
  EventMap[0xc6] = EVENT_OCTAVE;
  EventMap[0xc7] = EVENT_NOP;
  EventMap[0xc8] = EVENT_NOISE_FREQ;
  EventMap[0xc9] = EVENT_NOISE_ON;
  EventMap[0xca] = EVENT_NOISE_OFF;
  EventMap[0xcb] = EVENT_PITCH_MOD_ON;
  EventMap[0xcc] = EVENT_PITCH_MOD_OFF;
  EventMap[0xcd] = EVENT_JUMP_TO_SFX_LO;
  EventMap[0xce] = EVENT_JUMP_TO_SFX_HI;
  EventMap[0xcf] = EVENT_TUNING;
  EventMap[0xd0] = EVENT_END;
  EventMap[0xd1] = EVENT_TEMPO;
  if (version == SUZUKISNES_SD3) {
    EventMap[0xd2] = EVENT_LOOP_START; // duplicated
    EventMap[0xd3] = EVENT_LOOP_START; // duplicated
  }
  else {
    EventMap[0xd2] = EVENT_TIMER1_FREQ;
    EventMap[0xd3] = EVENT_TIMER1_FREQ_REL;
  }
  EventMap[0xd4] = EVENT_LOOP_START;
  EventMap[0xd5] = EVENT_LOOP_END;
  EventMap[0xd6] = EVENT_LOOP_BREAK;
  EventMap[0xd7] = EVENT_LOOP_POINT;
  EventMap[0xd8] = EVENT_ADSR_DEFAULT;
  EventMap[0xd9] = EVENT_ADSR_AR;
  EventMap[0xda] = EVENT_ADSR_DR;
  EventMap[0xdb] = EVENT_ADSR_SL;
  EventMap[0xdc] = EVENT_ADSR_SR;
  EventMap[0xdd] = EVENT_DURATION_RATE;
  EventMap[0xde] = EVENT_PROGCHANGE;
  EventMap[0xdf] = EVENT_NOISE_FREQ_REL;
  if (version == SUZUKISNES_SD3) {
    EventMap[0xe0] = EVENT_VOLUME;
  }
  else { // SUZUKISNES_BL, SUZUKISNES_SMR
    EventMap[0xe0] = EVENT_UNKNOWN1;
  }
  //EventMap[0xe1] = (SuzukiSnesSeqEventType)0;
  EventMap[0xe2] = EVENT_VOLUME;
  EventMap[0xe3] = EVENT_VOLUME_REL;
  EventMap[0xe4] = EVENT_VOLUME_FADE;
  EventMap[0xe5] = EVENT_PORTAMENTO;
  EventMap[0xe6] = EVENT_PORTAMENTO_TOGGLE;
  EventMap[0xe7] = EVENT_PAN;
  EventMap[0xe8] = EVENT_PAN_FADE;
  EventMap[0xe9] = EVENT_PAN_LFO_ON;
  EventMap[0xea] = EVENT_PAN_LFO_RESTART;
  EventMap[0xeb] = EVENT_PAN_LFO_OFF;
  EventMap[0xec] = EVENT_TRANSPOSE_ABS;
  EventMap[0xed] = EVENT_TRANSPOSE_REL;
  EventMap[0xee] = EVENT_PERC_ON;
  EventMap[0xef] = EVENT_PERC_OFF;
  EventMap[0xf0] = EVENT_VIBRATO_ON;
  EventMap[0xf1] = EVENT_VIBRATO_ON_WITH_DELAY;
  EventMap[0xf2] = EVENT_TEMPO_REL;
  EventMap[0xf3] = EVENT_VIBRATO_OFF;
  EventMap[0xf4] = EVENT_TREMOLO_ON;
  EventMap[0xf5] = EVENT_TREMOLO_ON_WITH_DELAY;
  if (version == SUZUKISNES_SD3) {
    EventMap[0xf6] = EVENT_OCTAVE_UP; // duplicated
  }
  else {
    EventMap[0xf6] = EVENT_UNKNOWN1;
  }
  EventMap[0xf7] = EVENT_TREMOLO_OFF;
  EventMap[0xf8] = EVENT_SLUR_ON;
  EventMap[0xf9] = EVENT_SLUR_OFF;
  EventMap[0xfa] = EVENT_ECHO_ON;
  EventMap[0xfb] = EVENT_ECHO_OFF;
  if (version == SUZUKISNES_SD3) {
    EventMap[0xfc] = EVENT_CALL_SFX_LO;
    EventMap[0xfd] = EVENT_CALL_SFX_HI;
    EventMap[0xfe] = EVENT_OCTAVE_UP; // duplicated
    EventMap[0xff] = EVENT_OCTAVE_UP; // duplicated
  }
  else if (version == SUZUKISNES_BL) {
    EventMap[0xfc] = EVENT_OCTAVE_UP; // duplicated
    EventMap[0xfd] = EVENT_OCTAVE_UP; // duplicated
    EventMap[0xfe] = EVENT_UNKNOWN0;
    EventMap[0xff] = EVENT_UNKNOWN0;
  }
  else if (version == SUZUKISNES_SMR) {
    EventMap[0xfc] = EVENT_UNKNOWN3;
    EventMap[0xfd] = EVENT_OCTAVE_UP; // duplicated
    EventMap[0xfe] = EVENT_UNKNOWN0;
    EventMap[0xff] = EVENT_OCTAVE_UP; // duplicated
  }
}

double SuzukiSnesSeq::getTempoInBPM(uint8_t tempo) {
  if (tempo != 0) {
    return (double) 60000000 / (125 * tempo * SEQ_PPQN);
  }
  else {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}

//  ***************
//  SuzukiSnesTrack
//  ***************

SuzukiSnesTrack::SuzukiSnesTrack(SuzukiSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
}

void SuzukiSnesTrack::resetVars() {
  SeqTrack::resetVars();

  octave = 6;
  spcVolume = 100;
  loopLevel = 0;
  infiniteLoopPoint = 0;
}

bool SuzukiSnesTrack::readEvent() {
  SuzukiSnesSeq *parentSeq = (SuzukiSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::stringstream desc;

  SuzukiSnesSeqEventType eventType = (SuzukiSnesSeqEventType) 0;
  std::map<uint8_t, SuzukiSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

    case EVENT_NOTE: // 0x00..0xc3
    {
      uint8_t durIndex = statusByte / 14;
      uint8_t noteIndex = statusByte % 14;

      uint32_t dur;
      if (durIndex == 13) {
        dur = readByte(curOffset++);
        if (parentSeq->version == SUZUKISNES_SD3)
          dur++;
      }
      else {
        dur = SuzukiSnesSeq::NOTE_DUR_TABLE[durIndex];
      }

      if (noteIndex < 12) {
        uint8_t note = octave * 12 + noteIndex;

        // TODO: percussion note

        addNoteByDur(beginOffset, curOffset - beginOffset, note, NOTE_VELOCITY, dur);
        addTime(dur);
      }
      else if (noteIndex == 13) {
        makePrevDurNoteEnd(getTime() + dur);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc.str().c_str(), Type::Tie, ICON_NOTE);
        addTime(dur);
      }
      else {
        addRest(beginOffset, curOffset - beginOffset, dur);
      }

      break;
    }

    case EVENT_OCTAVE_UP: {
      addIncrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_OCTAVE_DOWN: {
      addDecrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_OCTAVE: {
      uint8_t newOctave = readByte(curOffset++);
      addSetOctave(beginOffset, curOffset - beginOffset, newOctave);
      break;
    }

    case EVENT_NOP: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc.str().c_str(), Type::Misc, ICON_BINARY);
      break;
    }

    case EVENT_NOISE_FREQ: {
      uint8_t newNCK = readByte(curOffset++) & 0x1f;
      desc << "Noise Frequency (NCK): " << (int) newNCK;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Noise Frequency",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_CONTROL);
      break;
    }

    case EVENT_NOISE_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Noise On",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_CONTROL);
      break;
    }

    case EVENT_NOISE_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Noise Off",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PITCH_MOD_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Modulation On",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PITCH_MOD_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Modulation Off",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_CONTROL);
      break;
    }

    case EVENT_JUMP_TO_SFX_LO: {
      // TODO: EVENT_JUMP_TO_SFX_LO
      uint8_t sfxIndex = readByte(curOffset++);
      desc << "SFX: " << (int) sfxIndex;
      addUnknown(beginOffset, curOffset - beginOffset, "Jump to SFX (LOWORD)", desc.str().c_str());
      bContinue = false;
      break;
    }

    case EVENT_JUMP_TO_SFX_HI: {
      // TODO: EVENT_JUMP_TO_SFX_HI
      uint8_t sfxIndex = readByte(curOffset++);
      desc << "SFX: " << (int) sfxIndex;
      addUnknown(beginOffset, curOffset - beginOffset, "Jump to SFX (HIWORD)", desc.str().c_str());
      bContinue = false;
      break;
    }

    case EVENT_CALL_SFX_LO: {
      // TODO: EVENT_CALL_SFX_LO
      uint8_t sfxIndex = readByte(curOffset++);
      desc << "SFX: " << (int) sfxIndex;
      addUnknown(beginOffset, curOffset - beginOffset, "Call SFX (LOWORD)", desc.str().c_str());
      bContinue = false;
      break;
    }

    case EVENT_CALL_SFX_HI: {
      // TODO: EVENT_CALL_SFX_HI
      uint8_t sfxIndex = readByte(curOffset++);
      desc << "SFX: " << (int) sfxIndex;
      addUnknown(beginOffset, curOffset - beginOffset, "Call SFX (HIWORD)", desc.str().c_str());
      bContinue = false;
      break;
    }

    case EVENT_TUNING: {
      int8_t newTuning = readByte(curOffset++);
      addFineTuning(beginOffset, curOffset - beginOffset, (newTuning / 16.0) * 100.0);
      break;
    }

    case EVENT_END: {
      // TODO: add "return from SFX" handler
      if ((infiniteLoopPoint & 0xff00) != 0) {
        bContinue = addLoopForever(beginOffset, curOffset - beginOffset);
        curOffset = infiniteLoopPoint;
      }
      else {
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
      }
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = readByte(curOffset++);
      parentSeq->spcTempo = newTempo;
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM(parentSeq->spcTempo));
      break;
    }

    case EVENT_TEMPO_REL: {
      int8_t delta = readByte(curOffset++);
      parentSeq->spcTempo += delta;
      addTempoBPM(beginOffset,
                  curOffset - beginOffset,
                  parentSeq->getTempoInBPM(parentSeq->spcTempo),
                  "Tempo (Relative)");
      break;
    }

    case EVENT_TIMER1_FREQ: {
      uint8_t newFreq = readByte(curOffset++);
      desc << "Frequency: " << (0.125 * newFreq) << "ms";
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Timer 1 Frequency",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_TEMPO);
      break;
    }

    case EVENT_TIMER1_FREQ_REL: {
      int8_t delta = readByte(curOffset++);
      desc << "Frequency Delta: " << (0.125 * delta) << "ms";
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Timer 1 Frequency (Relative)",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_TEMPO);
      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = readByte(curOffset++);
      int realLoopCount = (count == 0) ? 256 : count;

      desc << "Loop Count: " << realLoopCount;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", desc.str().c_str(), Type::Loop, ICON_STARTREP);

      if (loopLevel >= SUZUKISNES_LOOP_LEVEL_MAX) {
        // stack overflow
        break;
      }

      loopStart[loopLevel] = curOffset;
      loopCount[loopLevel] = count - 1;
      loopOctave[loopLevel] = octave;
      loopLevel++;
      break;
    }

    case EVENT_LOOP_END: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc.str().c_str(), Type::Loop, ICON_ENDREP);

      if (loopLevel == 0) {
        // stack overflow
        break;
      }

      if (loopCount[loopLevel - 1] == 0) {
        // repeat end
        loopLevel--;
      }
      else {
        // repeat again
        octave = loopOctave[loopLevel - 1];
        loopEnd[loopLevel - 1] = curOffset;
        curOffset = loopStart[loopLevel - 1];
        loopCount[loopLevel - 1]--;
      }

      break;
    }

    case EVENT_LOOP_BREAK: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", desc.str().c_str(), Type::Loop, ICON_ENDREP);

      if (loopLevel == 0) {
        // stack overflow
        break;
      }

      if (loopCount[loopLevel - 1] == 0) {
        // repeat end
        curOffset = loopEnd[loopLevel - 1];
        loopLevel--;
      }

      break;
    }

    case EVENT_LOOP_POINT: {
      infiniteLoopPoint = curOffset;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Infinite Loop Point",
                      desc.str().c_str(),
                      Type::Loop,
                      ICON_STARTREP);
      break;
    }

    case EVENT_ADSR_DEFAULT: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Default ADSR",
                      desc.str().c_str(),
                      Type::Adsr,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_AR: {
      uint8_t newAR = readByte(curOffset++) & 15;
      desc << "AR: " << (int) newAR;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "ADSR Attack Rate",
                      desc.str().c_str(),
                      Type::Adsr,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_DR: {
      uint8_t newDR = readByte(curOffset++) & 7;
      desc << "DR: " << (int) newDR;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "ADSR Decay Rate",
                      desc.str().c_str(),
                      Type::Adsr,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_SL: {
      uint8_t newSL = readByte(curOffset++) & 7;
      desc << "SL: " << (int) newSL;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "ADSR Sustain Level",
                      desc.str().c_str(),
                      Type::Adsr,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_SR: {
      uint8_t newSR = readByte(curOffset++) & 15;
      desc << "SR: " << (int) newSR;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "ADSR Sustain Rate",
                      desc.str().c_str(),
                      Type::Adsr,
                      ICON_CONTROL);
      break;
    }

    case EVENT_DURATION_RATE: {
      // TODO: save duration rate and apply to note length
      uint8_t newDurRate = readByte(curOffset++);
      desc << "Duration Rate: " << (int) newDurRate;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate", desc.str().c_str(), Type::DurationNote);
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t newProg = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, newProg);
      break;
    }

    case EVENT_NOISE_FREQ_REL: {
      int8_t delta = readByte(curOffset++);
      desc << "Noise Frequency (NCK) Delta: " << (int) delta;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Noise Frequency (Relative)",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VOLUME: {
      uint8_t vol = readByte(curOffset++);
      spcVolume = vol & 0x7f;
      addVol(beginOffset, curOffset - beginOffset, spcVolume);
      break;
    }

    case EVENT_VOLUME_REL: {
      int8_t delta = readByte(curOffset++);
      spcVolume = (spcVolume + delta) & 0x7f;
      addVol(beginOffset, curOffset - beginOffset, spcVolume, "Volume (Relative)");
      break;
    }

    case EVENT_VOLUME_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      uint8_t vol = readByte(curOffset++);
      desc << "Fade Length: " << (int) fadeLength << "  Volume: " << (int) vol;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Volume Fade",
                      desc.str().c_str(),
                      Type::Volume,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PORTAMENTO: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      desc << "Arg1: " << (int) arg1 << "  Arg2: " << (int) arg2;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Portamento",
                      desc.str().c_str(),
                      Type::Portamento,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PORTAMENTO_TOGGLE: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Portamento On/Off",
                      desc.str().c_str(),
                      Type::Portamento,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PAN: {
      // For left pan, the engine will decrease right volume (linear), but will do nothing to left volume.
      // For right pan, the engine will do the opposite.
      // For center pan, it will not decrease any volumes.
      uint8_t pan = readByte(curOffset++);

      // TODO: correct midi pan value, apply volume scale
      addPan(beginOffset, curOffset - beginOffset, pan >> 1);
      break;
    }

    case EVENT_PAN_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      uint8_t pan = readByte(curOffset++);
      desc << "Fade Length: " << (int) fadeLength << "  Pan: " << (int) (pan >> 1);

      // TODO: correct midi pan value, apply volume scale, do pan slide
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan Fade", desc.str().c_str(), Type::Pan, ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO_ON: {
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      desc << "Depth: " << (int) lfoDepth << "  Rate: " << (int) lfoRate;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pan LFO",
                      desc.str().c_str(),
                      Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO_RESTART: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pan LFO Restart",
                      desc.str().c_str(),
                      Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pan LFO Off",
                      desc.str().c_str(),
                      Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TRANSPOSE_ABS: {
      // TODO: fraction part of transpose?
      int8_t newTranspose = readByte(curOffset++);
      int8_t semitones = newTranspose / 4;
      addTranspose(beginOffset, curOffset - beginOffset, semitones);
      break;
    }

    case EVENT_TRANSPOSE_REL: {
      // TODO: fraction part of transpose?
      int8_t newTranspose = readByte(curOffset++);
      int8_t semitones = newTranspose / 4;
      addTranspose(beginOffset, curOffset - beginOffset, transpose + semitones, "Transpose (Relative)");
      break;
    }

    case EVENT_PERC_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Percussion On",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PERC_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Percussion Off",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_ON: {
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      desc << "Depth: " << (int) lfoDepth << "  Rate: " << (int) lfoRate;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato",
                      desc.str().c_str(),
                      Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_ON_WITH_DELAY: {
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      uint8_t lfoDelay = readByte(curOffset++);
      desc << "Depth: " << (int) lfoDepth << "  Rate: " << (int) lfoRate << "  Delay: " << (int) lfoDelay;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato",
                      desc.str().c_str(),
                      Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato Off",
                      desc.str().c_str(),
                      Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TREMOLO_ON: {
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      desc << "Depth: " << (int) lfoDepth << "  Rate: " << (int) lfoRate;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tremolo",
                      desc.str().c_str(),
                      Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TREMOLO_ON_WITH_DELAY: {
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      uint8_t lfoDelay = readByte(curOffset++);
      desc << "Depth: " << (int) lfoDepth << "  Rate: " << (int) lfoRate << "  Delay: " << (int) lfoDelay;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tremolo",
                      desc.str().c_str(),
                      Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TREMOLO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tremolo Off",
                      desc.str().c_str(),
                      Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_SLUR_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Slur On",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_CONTROL);
      break;
    }

    case EVENT_SLUR_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Slur Off",
                      desc.str().c_str(),
                      Type::ChangeState,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo On", desc.str().c_str(), Type::Reverb, ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Off", desc.str().c_str(), Type::Reverb, ICON_CONTROL);
      break;
    }

    default: {
      auto descr = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }

  //ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}
