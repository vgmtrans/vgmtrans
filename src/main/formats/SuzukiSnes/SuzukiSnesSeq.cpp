#include "SuzukiSnesSeq.h"
#include "spdlog/fmt/fmt.h"

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
      auto trackName = fmt::format("Track Pointer {:d}", trackIndex + 1);
      header->addChild(curOffset, 2, trackName);

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

  std::string desc;

  SuzukiSnesSeqEventType eventType = (SuzukiSnesSeqEventType) 0;
  std::map<uint8_t, SuzukiSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc = fmt::format("Event: 0x{:02X}", statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}", statusByte, arg1);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}", statusByte, arg1, arg2);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}",
                         statusByte, arg1, arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      uint8_t arg4 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}  Arg4: {:d}",
                         statusByte, arg1, arg2, arg3, arg4);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
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
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc, Type::Tie);
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
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc, Type::Nop);
      break;
    }

    case EVENT_NOISE_FREQ: {
      uint8_t newNCK = readByte(curOffset++) & 0x1f;
      desc = fmt::format("Noise Frequency (NCK): {:d}", newNCK);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Frequency", desc, Type::Noise);
      break;
    }

    case EVENT_NOISE_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise On", desc, Type::Noise);
      break;
    }

    case EVENT_NOISE_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Off", desc, Type::Noise);
      break;
    }

    case EVENT_PITCH_MOD_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Modulation On", desc,
                      Type::ChangeState);
      break;
    }

    case EVENT_PITCH_MOD_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Modulation Off", desc,
                      Type::ChangeState);
      break;
    }

    case EVENT_JUMP_TO_SFX_LO: {
      // TODO: EVENT_JUMP_TO_SFX_LO
      uint8_t sfxIndex = readByte(curOffset++);
      desc = fmt::format("SFX: {:d}", sfxIndex);
      addUnknown(beginOffset, curOffset - beginOffset, "Jump to SFX (LOWORD)", desc);
      bContinue = false;
      break;
    }

    case EVENT_JUMP_TO_SFX_HI: {
      // TODO: EVENT_JUMP_TO_SFX_HI
      uint8_t sfxIndex = readByte(curOffset++);
      desc = fmt::format("SFX: {:d}", sfxIndex);
      addUnknown(beginOffset, curOffset - beginOffset, "Jump to SFX (HIWORD)", desc);
      bContinue = false;
      break;
    }

    case EVENT_CALL_SFX_LO: {
      // TODO: EVENT_CALL_SFX_LO
      uint8_t sfxIndex = readByte(curOffset++);
      desc = fmt::format("SFX: {:d}", sfxIndex);
      addUnknown(beginOffset, curOffset - beginOffset, "Call SFX (LOWORD)", desc);
      bContinue = false;
      break;
    }

    case EVENT_CALL_SFX_HI: {
      // TODO: EVENT_CALL_SFX_HI
      uint8_t sfxIndex = readByte(curOffset++);
      desc = fmt::format("SFX: {:d}", sfxIndex);
      addUnknown(beginOffset, curOffset - beginOffset, "Call SFX (HIWORD)", desc);
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
      desc = fmt::format("Frequency: {}ms", 0.125 * newFreq);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Timer 1 Frequency",
                      desc, Type::Tempo);
      break;
    }

    case EVENT_TIMER1_FREQ_REL: {
      int8_t delta = readByte(curOffset++);
      desc = fmt::format("Frequency Delta: {}ms", 0.125 * delta);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Timer 1 Frequency (Relative)",
                      desc, Type::ChangeState);
      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = readByte(curOffset++);
      int realLoopCount = (count == 0) ? 256 : count;

      desc = fmt::format("Loop Count: {:d}", realLoopCount);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", desc, Type::RepeatStart);

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
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc, Type::RepeatEnd);

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
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", desc, Type::RepeatEnd);

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
      addGenericEvent(beginOffset, curOffset - beginOffset, "Infinite Loop Point", desc,
                      Type::RepeatStart);
      break;
    }

    case EVENT_ADSR_DEFAULT: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Default ADSR", desc, Type::Adsr);
      break;
    }

      case EVENT_ADSR_AR: {
        uint8_t newAR = readByte(curOffset++) & 15;
        desc = fmt::format("AR: {:d}", newAR);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Attack Rate", desc, Type::Adsr);
        break;
      }

      case EVENT_ADSR_DR: {
        uint8_t newDR = readByte(curOffset++) & 7;
        desc = fmt::format("DR: {:d}", newDR);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Decay Rate", desc, Type::Adsr);
        break;
      }

      case EVENT_ADSR_SL: {
        uint8_t newSL = readByte(curOffset++) & 7;
        desc = fmt::format("SL: {:d}", newSL);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Sustain Level", desc, Type::Adsr);
        break;
      }

      case EVENT_ADSR_SR: {
        uint8_t newSR = readByte(curOffset++) & 15;
        desc = fmt::format("SR: {:d}", newSR);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Sustain Rate", desc, Type::Adsr);
        break;
      }

      case EVENT_DURATION_RATE: {
        // TODO: save duration rate and apply to note length
        uint8_t newDurRate = readByte(curOffset++);
        desc = fmt::format("Duration Rate: {:d}", newDurRate);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate", desc, Type::DurationNote);
        break;
      }

    case EVENT_PROGCHANGE: {
      uint8_t newProg = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, newProg);
      break;
    }

    case EVENT_NOISE_FREQ_REL: {
      int8_t delta = readByte(curOffset++);
      desc = fmt::format("Noise Frequency (NCK) Delta: {:d}", delta);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Frequency (Relative)", desc,
                      Type::Noise);
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
        desc = fmt::format("Fade Length: {:d}  Volume: {:d}", fadeLength, vol);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Volume Fade", desc, Type::VolumeSlide);
        break;
      }

      case EVENT_PORTAMENTO: {
        uint8_t arg1 = readByte(curOffset++);
        uint8_t arg2 = readByte(curOffset++);
        desc = fmt::format("Arg1: {:d}  Arg2: {:d}", arg1, arg2);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento", desc, Type::Portamento);
        break;
      }

    case EVENT_PORTAMENTO_TOGGLE: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento On/Off", desc, Type::Portamento);
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
        desc = fmt::format("Fade Length: {:d}  Pan: {:d}", fadeLength, pan >> 1);

        // TODO: correct midi pan value, apply volume scale, do pan slide
        addGenericEvent(beginOffset, curOffset - beginOffset, "Pan Fade", desc, Type::PanSlide);
        break;
      }

      case EVENT_PAN_LFO_ON: {
        uint8_t lfoDepth = readByte(curOffset++);
        uint8_t lfoRate = readByte(curOffset++);
        desc = fmt::format("Depth: {:d}  Rate: {:d}", lfoDepth, lfoRate);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO", desc, Type::PanLfo);
        break;
    }

    case EVENT_PAN_LFO_RESTART: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO Restart", desc, Type::PanLfo);
      break;
    }

    case EVENT_PAN_LFO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO Off", desc, Type::PanLfo);
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
      addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion On", desc, Type::UseDrumKit);
      break;
    }

    case EVENT_PERC_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion Off", desc, Type::UseDrumKit);
      break;
    }

    case EVENT_VIBRATO_ON: {
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      desc = fmt::format("Depth: {:d}  Rate: {:d}", lfoDepth, lfoRate);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO_ON_WITH_DELAY: {
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      uint8_t lfoDelay = readByte(curOffset++);
      desc = fmt::format("Depth: {:d}  Rate: {:d}  Delay: {:d}", lfoDepth, lfoRate, lfoDelay);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Off", desc, Type::Vibrato);
      break;
    }

    case EVENT_TREMOLO_ON: {
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      desc = fmt::format("Depth: {:d}  Rate: {:d}", lfoDepth, lfoRate);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo", desc, Type::Tremelo);
      break;
    }

    case EVENT_TREMOLO_ON_WITH_DELAY: {
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      uint8_t lfoDelay = readByte(curOffset++);
      desc = fmt::format("Depth: {:d}  Rate: {:d}  Delay: {:d}", lfoDepth, lfoRate, lfoDelay);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo", desc, Type::Tremelo);
      break;
    }

    case EVENT_TREMOLO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Off", desc, Type::Tremelo);
      break;
    }

    case EVENT_SLUR_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Slur On", desc, Type::Portamento);
      break;
    }

    case EVENT_SLUR_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Slur Off", desc, Type::Portamento);
      break;
    }

    case EVENT_ECHO_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo On", desc, Type::Reverb);
      break;
    }

    case EVENT_ECHO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Off", desc, Type::Reverb);
      break;
    }

    default: {
      auto descr = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }


  return bContinue;
}
