/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiSnesSeq.h"
#include "ScaleConversion.h"
#include "spdlog/fmt/fmt.h"

DECLARE_FORMAT(KonamiSnes);

//  **********
//  KonamiSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    48

const uint8_t KonamiSnesSeq::PAN_VOLUME_LEFT_V1[] = {
    0x00, 0x05, 0x0c, 0x14, 0x1e, 0x28, 0x32, 0x3c,
    0x46, 0x50, 0x59, 0x62, 0x69, 0x6f, 0x74, 0x78,
    0x7b, 0x7d, 0x7e, 0x7e, 0x7f
};

const uint8_t KonamiSnesSeq::PAN_VOLUME_RIGHT_V1[] = {
    0x7f, 0x7e, 0x7e, 0x7d, 0x7b, 0x78, 0x74, 0x6f,
    0x69, 0x62, 0x59, 0x50, 0x46, 0x3c, 0x32, 0x28,
    0x1e, 0x14, 0x0c, 0x05, 0x00
};

const uint8_t KonamiSnesSeq::PAN_VOLUME_LEFT_V2[] = {
    0x00, 0x0a, 0x18, 0x28, 0x3c, 0x50, 0x64, 0x78,
    0x8c, 0xa0, 0xb2, 0xc4, 0xd2, 0xde, 0xe8, 0xf0,
    0xf6, 0xfa, 0xfc, 0xfc, 0xfe
};

const uint8_t KonamiSnesSeq::PAN_VOLUME_RIGHT_V2[] = {
    0xfe, 0xfc, 0xfc, 0xfa, 0xf6, 0xf0, 0xe8, 0xde,
    0xd2, 0xc4, 0xb2, 0xa0, 0x8c, 0x78, 0x64, 0x50,
    0x3c, 0x28, 0x18, 0x0a, 0x00
};

// pan table (compatible with Nintendo engine)
const uint8_t KonamiSnesSeq::PAN_TABLE[] = {
    0x00, 0x04, 0x08, 0x0e, 0x14, 0x1a, 0x20, 0x28,
    0x30, 0x38, 0x40, 0x48, 0x50, 0x5a, 0x64, 0x6e,
    0x78, 0x82, 0x8c, 0x96, 0xa0, 0xa8, 0xb0, 0xb8,
    0xc0, 0xc8, 0xd0, 0xd6, 0xdc, 0xe0, 0xe4, 0xe8,
    0xec, 0xf0, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0xfe,
    0xfe, 0xfe
};

KonamiSnesSeq::KonamiSnesSeq(RawFile *file, KonamiSnesVersion ver, uint32_t seqdataOffset, std::string newName)
    : VGMSeq(KonamiSnesFormat::name, file, seqdataOffset, 0, newName), version(ver) {
  setAllowDiscontinuousTrackData(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

KonamiSnesSeq::~KonamiSnesSeq(void) {
}

void KonamiSnesSeq::resetVars(void) {
  VGMSeq::resetVars();

  tempo = 0;
}

bool KonamiSnesSeq::parseHeader(void) {
  setPPQN(SEQ_PPQN);

  // Number of tracks can be less than 8.
  // For instance: Ganbare Goemon 3 - Title
  nNumTracks = MAX_TRACKS;

  VGMHeader *seqHeader = addHeader(dwOffset, nNumTracks * 2, "Sequence Header");
  for (uint32_t trackNumber = 0; trackNumber < nNumTracks; trackNumber++) {
    uint32_t trackPointerOffset = dwOffset + (trackNumber * 2);
    if (trackPointerOffset + 2 > 0x10000) {
      return false;
    }

    uint16_t trkOff = readShort(trackPointerOffset);
    seqHeader->addPointer(trackPointerOffset, 2, trkOff, true, "Track Pointer");

    assert(trkOff >= dwOffset);

    if (trkOff - dwOffset < nNumTracks * 2) {
      nNumTracks = (trkOff - dwOffset) / 2;
    }
  }
  seqHeader->unLength = nNumTracks * 2;

  return true;
}


bool KonamiSnesSeq::parseTrackPointers(void) {
  for (uint32_t trackNumber = 0; trackNumber < nNumTracks; trackNumber++) {
    uint16_t trkOff = readShort(dwOffset + trackNumber * 2);
    aTracks.push_back(new KonamiSnesTrack(this, trkOff));
  }
  return true;
}

void KonamiSnesSeq::loadEventMap() {
  if (version == KONAMISNES_V1) {
    NOTE_DUR_RATE_MAX = 100;
  }
  else {
    NOTE_DUR_RATE_MAX = 127;
  }

  for (uint8_t statusByte = 0x00; statusByte <= 0x5f; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
    EventMap[statusByte | 0x80] = EVENT_NOTE;
  }

  EventMap[0x60] = EVENT_PERCUSSION_ON;
  EventMap[0x61] = EVENT_PERCUSSION_OFF;

  if (version == KONAMISNES_V1) {
    EventMap[0x62] = EVENT_UNKNOWN1;
    EventMap[0x63] = EVENT_UNKNOWN1;
    EventMap[0x64] = EVENT_UNKNOWN2;

    for (uint8_t statusByte = 0x65; statusByte <= 0x7f; statusByte++) {
      EventMap[statusByte] = EVENT_UNKNOWN0;
    }
  }
  else {
    EventMap[0x62] = EVENT_GAIN;

    for (uint8_t statusByte = 0x63; statusByte <= 0x7f; statusByte++) {
      EventMap[statusByte] = EVENT_UNKNOWN0;
    }
  }

  EventMap[0xe0] = EVENT_REST;
  EventMap[0xe1] = EVENT_TIE;
  EventMap[0xe2] = EVENT_PROGCHANGE;
  EventMap[0xe3] = EVENT_PAN;
  EventMap[0xe4] = EVENT_VIBRATO;
  EventMap[0xe5] = EVENT_RANDOM_PITCH;
  EventMap[0xe6] = EVENT_LOOP_START;
  EventMap[0xe7] = EVENT_LOOP_END;
  EventMap[0xe8] = EVENT_LOOP_START_2;
  EventMap[0xe9] = EVENT_LOOP_END_2;
  EventMap[0xea] = EVENT_TEMPO;
  EventMap[0xeb] = EVENT_TEMPO_FADE;
  EventMap[0xec] = EVENT_TRANSPABS;
  EventMap[0xed] = EVENT_ADSR1;
  EventMap[0xee] = EVENT_VOLUME;
  EventMap[0xef] = EVENT_VOLUME_FADE;
  EventMap[0xf0] = EVENT_PORTAMENTO;
  EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V2;
  EventMap[0xf2] = EVENT_TUNING;
  EventMap[0xf3] = EVENT_PITCH_SLIDE_V3;
  EventMap[0xf4] = EVENT_ECHO;
  EventMap[0xf5] = EVENT_ECHO_PARAM;
  EventMap[0xf6] = EVENT_LOOP_WITH_VOLTA_START;
  EventMap[0xf7] = EVENT_LOOP_WITH_VOLTA_END;
  EventMap[0xf8] = EVENT_PAN_FADE;
  EventMap[0xf9] = EVENT_VIBRATO_FADE;
  EventMap[0xfa] = EVENT_ADSR_GAIN;
  EventMap[0xfb] = EVENT_ADSR2;
  EventMap[0xfc] = EVENT_PROGCHANGEVOL;
  EventMap[0xfd] = EVENT_GOTO;
  EventMap[0xfe] = EVENT_CALL;
  EventMap[0xff] = EVENT_END;

  switch (version) {
    case KONAMISNES_V1:
      EventMap[0xed] = EVENT_UNKNOWN3; // nop
      EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V1;
      EventMap[0xf3] = EVENT_PITCH_SLIDE_V1;
      EventMap[0xfa] = EVENT_UNKNOWN3;
      EventMap[0xfb] = EVENT_UNKNOWN1;
      EventMap.erase(0xfc); // game-specific?
      break;

    case KONAMISNES_V2:
      EventMap[0xed] = EVENT_UNKNOWN3; // nop
      EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V1;
      EventMap[0xf3] = EVENT_PITCH_SLIDE_V2;
      EventMap[0xfa] = EVENT_UNKNOWN3;
      EventMap[0xfb] = EVENT_UNKNOWN1;
      EventMap.erase(0xfc); // game-specific?
      break;

    case KONAMISNES_V3:
      EventMap[0xed] = EVENT_UNKNOWN3; // nop
      EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V1;
      EventMap[0xf3] = EVENT_PITCH_SLIDE_V2;
      EventMap[0xfa] = EVENT_UNKNOWN3;
      EventMap[0xfb] = EVENT_UNKNOWN1;
      EventMap[0xfc] = EVENT_UNKNOWN2;
      break;

    case KONAMISNES_V4:
      EventMap[0xed] = EVENT_UNKNOWN3;
      EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V2;
      EventMap[0xf3] = EVENT_PITCH_SLIDE_V2;
      EventMap[0xfa] = EVENT_ADSR_GAIN;
      EventMap[0xfb] = EVENT_ADSR2;
      EventMap[0xfc] = EVENT_PROGCHANGEVOL;
      break;

    case KONAMISNES_V5:
      for (uint8_t statusByte = 0x70; statusByte <= 0x7f; statusByte++) {
        EventMap[statusByte] = EVENT_INSTANT_TUNING;
      }

      EventMap[0xed] = EVENT_ADSR1;
      EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V2;
      EventMap[0xf3] = EVENT_PITCH_SLIDE_V3;
      EventMap[0xfa] = EVENT_ADSR_GAIN;
      EventMap[0xfb] = EVENT_ADSR2;
      EventMap[0xfc] = EVENT_PROGCHANGEVOL;
      break;

    case KONAMISNES_V6:
      for (uint8_t statusByte = 0x70; statusByte <= 0x7f; statusByte++) {
        EventMap[statusByte] = EVENT_INSTANT_TUNING;
      }

      EventMap[0xed] = EVENT_ADSR1;
      EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V2;
      EventMap[0xf3] = EVENT_PITCH_SLIDE_V3;
      EventMap[0xfa] = EVENT_ADSR_GAIN;
      EventMap[0xfb] = EVENT_ADSR2;
      EventMap[0xfc] = EVENT_PROGCHANGEVOL;
      break;

    default:
      L_WARN("Unknown version of Konami SNES format");
      break;
  }
}

double KonamiSnesSeq::getTempoInBPM() {
  return getTempoInBPM(tempo);
}

double KonamiSnesSeq::getTempoInBPM(uint8_t tempo) {
  if (tempo != 0) {
    uint8_t timerFreq;
    if (version == KONAMISNES_V1) {
      timerFreq = 0x20;
    }
    else {
      timerFreq = 0x40;
    }

    return 60000000.0 / (SEQ_PPQN * (125 * timerFreq)) * (tempo / 256.0);
  }
  else {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}


//  ************
//  KonamiSnesTrack
//  ************

KonamiSnesTrack::KonamiSnesTrack(KonamiSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void KonamiSnesTrack::resetVars(void) {
  SeqTrack::resetVars();

  inSubroutine = false;
  loopCount = 0;
  loopVolumeDelta = 0;
  loopPitchDelta = 0;
  loopCount2 = 0;
  loopVolumeDelta2 = 0;
  loopPitchDelta2 = 0;
  voltaEndMeansPlayFromStart = false;
  voltaEndMeansPlayNextVolta = false;
  percussion = false;

  noteLength = 0;
  noteDurationRate = 0;
  subReturnAddr = 0;
  loopReturnAddr = 0;
  loopReturnAddr2 = 0;
  voltaLoopStart = 0;
  voltaLoopEnd = 0;
  instrument = 0;

  prevNoteKey = -1;
  prevNoteSlurred = false;
}

double KonamiSnesTrack::getTuningInSemitones(int8_t tuning) {
  return tuning * 4 / 256.0;
}


uint8_t KonamiSnesTrack::convertGAINAmountToGAIN(uint8_t gainAmount) {
  uint8_t gain = gainAmount;
  if (gainAmount >= 200) {
    // exponential decrease
    gain = 0x80 | ((gainAmount - 200) & 0x1f);
  }
  else if (gainAmount >= 100) {
    // linear decrease
    gain = 0xa0 | ((gainAmount - 100) & 0x1f);
  }
  return gain;
}

bool KonamiSnesTrack::readEvent(void) {
  KonamiSnesSeq *parentSeq = (KonamiSnesSeq *) this->parentSeq;
  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  KonamiSnesSeqEventType eventType = (KonamiSnesSeqEventType) 0;
  std::map<uint8_t, KonamiSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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
      desc = fmt::format(
          "Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}",
          statusByte, arg1, arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      uint8_t arg4 = readByte(curOffset++);
      desc = fmt::format(
          "Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}  Arg4: {:d}",
          statusByte, arg1, arg2, arg3, arg4);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN5: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      uint8_t arg4 = readByte(curOffset++);
      uint8_t arg5 = readByte(curOffset++);
      desc = fmt::format(
          "Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}  Arg4: {:d}  Arg5: {:d}",
          statusByte, arg1, arg2, arg3, arg4, arg5);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_NOTE: {
      bool hasNoteLength = ((statusByte & 0x80) == 0);
      uint8_t key = statusByte & 0x7f;

      uint8_t len;
      if (hasNoteLength) {
        len = readByte(curOffset++);
        noteLength = len;
      }
      else {
        len = noteLength;
      }

      uint8_t vel;
      vel = readByte(curOffset++);
      bool hasNoteDuration = ((vel & 0x80) == 0);
      if (hasNoteDuration) {
        noteDurationRate = std::min(vel, parentSeq->NOTE_DUR_RATE_MAX);
        vel = readByte(curOffset++);
      }
      vel &= 0x7f;

      if (vel == 0) {
        vel = 1; // TODO: verification
      }

      uint8_t dur = len;
      if (noteDurationRate != parentSeq->NOTE_DUR_RATE_MAX) {
        if (parentSeq->version == KONAMISNES_V1) {
          dur = (len * noteDurationRate) / 100;
        }
        else {
          dur = (len * (noteDurationRate << 1)) >> 8;
        }

        if (dur == 0) {
          dur = 1;
        }
      }

      if (prevNoteSlurred && key == prevNoteKey) {
        // TODO: Note volume can be changed during a tied note
        // See the end of Konami Logo sequence for example
        makePrevDurNoteEnd(getTime() + dur);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc, Type::Tie);
      }
      else {
        addNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur);
        prevNoteKey = key;
      }
      prevNoteSlurred = (noteDurationRate == parentSeq->NOTE_DUR_RATE_MAX);
      addTime(len);

      break;
    }

    case EVENT_PERCUSSION_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion On", desc, Type::ChangeState);
      if (!percussion) {
        addProgramChange(beginOffset, curOffset - beginOffset, 127 << 7, true);
        percussion = true;
      }
      break;
    }

    case EVENT_PERCUSSION_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion Off", desc, Type::ChangeState);
      if (percussion) {
        addProgramChange(beginOffset, curOffset - beginOffset, instrument, true);
        percussion = false;
      }
      break;
    }

    case EVENT_GAIN: {
      uint8_t newGAINAmount = readByte(curOffset++);
      uint8_t newGAIN = convertGAINAmountToGAIN(newGAINAmount);

      desc = fmt::format("GAIN: {} (${:02X})", newGAINAmount, newGAIN);
      addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN", desc, Type::Adsr);
      break;
    }

    case EVENT_INSTANT_TUNING: {
      int8_t newTuning = statusByte & 0x0f;
      if (newTuning > 8) {
        // extend sign
        newTuning -= 16;
      }

      double cents = getTuningInSemitones(newTuning) * 100.0;
      addFineTuning(beginOffset, curOffset - beginOffset, cents, "Instant Fine Tuning");
      break;
    }

    case EVENT_REST: {
      noteLength = readByte(curOffset++);
      addRest(beginOffset, curOffset - beginOffset, noteLength);
      prevNoteSlurred = false;
      break;
    }

    case EVENT_TIE: {
      noteLength = readByte(curOffset++);
      noteDurationRate = readByte(curOffset++);
      noteDurationRate = std::min(noteDurationRate, parentSeq->NOTE_DUR_RATE_MAX);
      if (prevNoteSlurred) {
        uint8_t dur = noteLength;
        if (noteDurationRate < parentSeq->NOTE_DUR_RATE_MAX) {
          if (parentSeq->version == KONAMISNES_V1) {
            dur = (noteLength * noteDurationRate) / 100;
          }
          else {
            dur = (noteLength * (noteDurationRate << 1)) >> 8;
          }

          if (dur == 0) {
            dur = 1;
          }
        }

        makePrevDurNoteEnd(getTime() + dur);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc, Type::Tie);
        addTime(noteLength);
        prevNoteSlurred = (noteDurationRate == parentSeq->NOTE_DUR_RATE_MAX);
      }
      else {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc, Type::Tie);
        addTime(noteLength);
      }
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t newProg = readByte(curOffset++);

      instrument = newProg;
      addProgramChange(beginOffset, curOffset - beginOffset, newProg, true);
      addPanNoItem(64); // TODO: apply true pan from instrument table
      break;
    }

    case EVENT_PROGCHANGEVOL: {
      uint8_t newVolume = readByte(curOffset++);
      uint8_t newProg = readByte(curOffset++);

      instrument = newProg;
      addProgramChange(beginOffset, curOffset - beginOffset, newProg, true);

      uint8_t midiVolume = convertPercentAmpToStdMidiVal(newVolume / 255.0);
      addVolNoItem(midiVolume);
      addPanNoItem(64); // TODO: apply true pan from instrument table
      break;
    }

    case EVENT_PAN: {
      uint8_t newPan = readByte(curOffset++);

      bool instrumentPanOff;
      bool instrumentPanOn;
      switch (parentSeq->version) {
        case KONAMISNES_V1:
        case KONAMISNES_V2:
          instrumentPanOff = (newPan == 0x15);
          instrumentPanOn = (newPan == 0x16);
          break;

        default:
          instrumentPanOff = (newPan == 0x2a);
          instrumentPanOn = (newPan == 0x2c);
      }

      if (instrumentPanOff) {
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Per-Instrument Pan Off",
                        desc,
                        Type::Pan);
      }
      else if (instrumentPanOn) {
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Per-Instrument Pan On",
                        desc,
                        Type::Pan);
      }
      else {
        uint8_t volumeLeft;
        uint8_t volumeRight;
        switch (parentSeq->version) {
          case KONAMISNES_V1:
          case KONAMISNES_V2: {
            const uint8_t *PAN_VOLUME_LEFT;
            const uint8_t *PAN_VOLUME_RIGHT;
            if (parentSeq->version == KONAMISNES_V1) {
              PAN_VOLUME_LEFT = parentSeq->PAN_VOLUME_LEFT_V1;
              PAN_VOLUME_RIGHT = parentSeq->PAN_VOLUME_RIGHT_V1;
            }
            else { // KONAMISNES_V2
              PAN_VOLUME_LEFT = parentSeq->PAN_VOLUME_LEFT_V2;
              PAN_VOLUME_RIGHT = parentSeq->PAN_VOLUME_RIGHT_V2;
            }

            newPan = std::min(newPan, (uint8_t) 20);
            volumeLeft = PAN_VOLUME_LEFT[newPan];
            volumeRight = PAN_VOLUME_RIGHT[newPan];
            break;
          }

          default:
            newPan = std::min(newPan, (uint8_t) 40);
            volumeLeft = KonamiSnesSeq::PAN_TABLE[40 - newPan];
            volumeRight = KonamiSnesSeq::PAN_TABLE[newPan];
        }

        double linearPan = (double) volumeRight / (volumeLeft + volumeRight);
        uint8_t midiPan = convertLinearPercentPanValToStdMidiVal(linearPan);

        // TODO: apply volume scale
        addPan(beginOffset, curOffset - beginOffset, midiPan);
      }
      break;
    }

    case EVENT_VIBRATO: {
      uint8_t vibratoDelay = readByte(curOffset++);
      uint8_t vibratoRate = readByte(curOffset++);
      uint8_t vibratoDepth = readByte(curOffset++);
      desc = fmt::format(
          "Delay: {:d}  Rate: {:d}  Depth: {:d}",
          vibratoDelay, vibratoRate, vibratoDepth);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato",
                      desc,
                      Type::Vibrato);
      break;
    }

    case EVENT_RANDOM_PITCH: {
      uint8_t envRate = readByte(curOffset++);
      uint16_t envPitchMask = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format(
          "Rate: {:d}  Pitch Mask: ${:04X}", envRate, envPitchMask);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Random Pitch",
                      desc,
                      Type::Modulation);
      break;
    }

      case EVENT_LOOP_START: {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", desc, Type::RepeatStart);
      loopReturnAddr = curOffset;
      break;
    }

    case EVENT_LOOP_END: {
      uint8_t times = readByte(curOffset++);
      int8_t volumeDelta = readByte(curOffset++);
      int8_t pitchDelta = readByte(curOffset++);

      desc = fmt::format(
          "Times: {:d}  Volume Delta: {:d}  Pitch Delta: {:d}",
          times, volumeDelta, pitchDelta);
      if (times == 0) {
        bContinue = addLoopForever(beginOffset, curOffset - beginOffset, "Loop End");
      }
      else {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc, Type::RepeatStart);
      }

      bool loopAgain;
      if (times == 0) {
        // infinite loop
        loopAgain = true;
      }
      else {
        loopCount++;
        loopAgain = (loopCount != times);
      }

      if (loopAgain) {
        curOffset = loopReturnAddr;
        loopVolumeDelta += volumeDelta;
        loopPitchDelta += pitchDelta;

        assert(loopReturnAddr != 0);
      }
      else {
        loopCount = 0;
        loopVolumeDelta = 0;
        loopPitchDelta = 0;
      }
      break;
    }

      case EVENT_LOOP_START_2: {
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Loop Start #2",
                        desc,
                        Type::RepeatStart);
        loopReturnAddr2 = curOffset;
        break;
      }

    case EVENT_LOOP_END_2: {
      uint8_t times = readByte(curOffset++);
      int8_t volumeDelta = readByte(curOffset++);
      int8_t pitchDelta = readByte(curOffset++);

      desc = fmt::format(
          "Times: {:d}  Volume Delta: {:d}  Pitch Delta: {:d}",
          times, volumeDelta, pitchDelta);
      if (times == 0) {
        bContinue = addLoopForever(beginOffset, curOffset - beginOffset, "Loop End #2");
      }
      else {
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "Loop End #2",
                          desc,
                          Type::RepeatStart);
      }

      bool loopAgain;
      if (times == 0) {
        // infinite loop
        loopAgain = true;
      }
      else {
        loopCount2++;
        loopAgain = (loopCount2 != times);
      }

      if (loopAgain) {
        curOffset = loopReturnAddr2;
        loopVolumeDelta2 += volumeDelta;
        loopPitchDelta2 += pitchDelta;

        assert(loopReturnAddr2 != 0);
      }
      else {
        loopCount2 = 0;
        loopVolumeDelta2 = 0;
        loopPitchDelta2 = 0;
      }
      break;
    }

    case EVENT_TEMPO: {
      // actual Konami engine has tempo for each tracks,
      // here we set the song speed as a global tempo
      uint8_t newTempo = readByte(curOffset++);
      parentSeq->tempo = newTempo;
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM());
      break;
    }

    case EVENT_TEMPO_FADE: {
      uint8_t newTempo = readByte(curOffset++);
      uint8_t fadeSpeed = readByte(curOffset++);
      desc = fmt::format(
          "BPM: {}  Fade Length: {}",
          parentSeq->getTempoInBPM(newTempo), fadeSpeed);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tempo Fade", desc, Type::Tempo);
      break;
    }

    case EVENT_TRANSPABS: {
      int8_t newTransp = (int8_t) readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, newTransp);
      break;
    }

    case EVENT_ADSR1: {
      uint8_t newADSR1 = readByte(curOffset++);
      desc = fmt::format("ADSR(1): ${:02X}", newADSR1);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR(1)", desc, Type::Adsr);
      break;
    }

    case EVENT_ADSR2: {
      uint8_t newADSR2 = readByte(curOffset++);
      desc = fmt::format("ADSR(2): ${:02X}", newADSR2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR(2)", desc, Type::Adsr);
      break;
    }

    case EVENT_VOLUME: {
      uint8_t newVolume = readByte(curOffset++);
      uint8_t midiVolume = convertPercentAmpToStdMidiVal(newVolume / 255.0);
      addVol(beginOffset, curOffset - beginOffset, midiVolume);
      break;
    }

    case EVENT_VOLUME_FADE: {
      uint8_t newVolume = readByte(curOffset++);
      uint8_t fadeSpeed = readByte(curOffset++);
      desc = fmt::format("Volume: {:d}  Fade Length: {:d}", newVolume, fadeSpeed);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Volume Fade",
                      desc,
                      Type::VolumeSlide);
      break;
    }

    case EVENT_PORTAMENTO: {
      uint8_t portamentoSpeed = readByte(curOffset++);
      desc = fmt::format("Portamento Speed: {:d}", portamentoSpeed);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Portamento",
                      desc,
                      Type::Portamento);
      break;
    }

    case EVENT_PITCH_ENVELOPE_V1: {
      uint8_t pitchEnvDelay = readByte(curOffset++);
      uint8_t pitchEnvSpeed = readByte(curOffset++);
      uint8_t pitchEnvDepth = readByte(curOffset++);
      desc = fmt::format(
          "Delay: {:d}  Speed: {:d}  Depth: {:d}",
          pitchEnvDelay, pitchEnvSpeed, -pitchEnvDepth);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Envelope",
                      desc,
                      Type::PitchEnvelope);
      break;
    }

    case EVENT_PITCH_ENVELOPE_V2: {
      uint8_t pitchEnvDelay = readByte(curOffset++);
      uint8_t pitchEnvLength = readByte(curOffset++);
      uint8_t pitchEnvOffset = readByte(curOffset++);
      int16_t pitchDelta = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format(
          "Delay: {:d}  Length: {:d}  Offset: {:d} semitones  Delta: {:.1f} semitones",
          pitchEnvDelay, pitchEnvLength, -pitchEnvOffset, pitchDelta / 256.0);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Envelope",
                      desc,
                      Type::PitchEnvelope);
      break;
    }

    case EVENT_TUNING: {
      int8_t newTuning = (int8_t) readByte(curOffset++);
      double cents = getTuningInSemitones(newTuning) * 100.0;
      addFineTuning(beginOffset, curOffset - beginOffset, cents);
      break;
    }

    case EVENT_PITCH_SLIDE_V1: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc = fmt::format("Arg1: {:d}  Arg2: {:d}  Arg3: {:d}", arg1, arg2, arg3);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Slide",
                      desc,
                      Type::PitchBendSlide);
      break;
    }

    case EVENT_PITCH_SLIDE_V2: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc = fmt::format("Arg1: {:d}  Arg2: {:d}  Arg3: {:d}", arg1, arg2, arg3);

      if (arg2 != 0) {
        uint8_t arg4 = readByte(curOffset++);
        uint8_t arg5 = readByte(curOffset++);
        uint8_t arg6 = readByte(curOffset++);
        fmt::format_to(std::back_inserter(desc), "  Arg4: {:d}  Arg5: {:d}  Arg6: {:d}", arg4, arg5, arg6);
      }

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Slide",
                      desc,
                      Type::PitchBendSlide);
      break;
    }

    case EVENT_PITCH_SLIDE_V3: {
      uint8_t pitchSlideDelay = readByte(curOffset++);
      uint8_t pitchSlideLength = readByte(curOffset++);
      uint8_t pitchSlideNote = readByte(curOffset++);
      int16_t pitchDelta = readShort(curOffset);
      curOffset += 2;

      uint8_t pitchSlideNoteNumber = (pitchSlideNote & 0x7f) + transpose;

      desc = fmt::format(
          "Delay: {:d}  Length: {:d}  Final Note: {:d}  Delta: {:.1f} semitones",
          pitchSlideDelay, pitchSlideLength, pitchSlideNoteNumber, pitchDelta / 256.0);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Slide",
                      desc,
                      Type::PitchBendSlide);
      break;
    }

    case EVENT_ECHO: {
      uint8_t echoChannels = readByte(curOffset++);
      uint8_t echoVolumeL = readByte(curOffset++);
      uint8_t echoVolumeR = readByte(curOffset++);

      desc = fmt::format(
          "EON: {:d}  EVOL(L): {:d}  EVOL(R): {:d}",
          echoChannels, echoVolumeL, echoVolumeR);

      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo", desc, Type::Reverb);
      break;
    }

    case EVENT_ECHO_PARAM: {
      uint8_t echoDelay = readByte(curOffset++);
      uint8_t echoFeedback = readByte(curOffset++);
      uint8_t echoArg3 = readByte(curOffset++);

      desc = fmt::format(
          "EDL: {:d}  EFB: {:d}  Arg3: {:d}",
          echoDelay, echoFeedback, echoArg3);

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Param",
                      desc,
                      Type::Reverb);
      break;
    }

    case EVENT_LOOP_WITH_VOLTA_START: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Loop With Volta Start",
                      desc,
                      Type::RepeatStart);

      voltaLoopStart = curOffset;
      voltaEndMeansPlayFromStart = false;
      voltaEndMeansPlayNextVolta = false;
      break;
    }

    case EVENT_LOOP_WITH_VOLTA_END: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Loop With Volta End",
                      desc,
                      Type::RepeatStart);

      if (voltaEndMeansPlayFromStart) {
        // second time - end of first volta bracket: play from start
        voltaEndMeansPlayFromStart = false;
        voltaEndMeansPlayNextVolta = true;
        voltaLoopEnd = curOffset;
        curOffset = voltaLoopStart;
      }
      else if (voltaEndMeansPlayNextVolta) {
        // third time - start of first volta bracket: play the new bracket (or quit from the entire loop)
        voltaEndMeansPlayFromStart = true;
        voltaEndMeansPlayNextVolta = false;
        curOffset = voltaLoopEnd;
      }
      else {
        // first time - start of first volta bracket: just play it
        voltaEndMeansPlayFromStart = true;
      }
      break;
    }

    case EVENT_PAN_FADE: {
      uint8_t newPan = readByte(curOffset++);
      uint8_t fadeSpeed = readByte(curOffset++);
      desc = fmt::format("Pan: {:d}  Fade Length: {:d}", newPan, fadeSpeed);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan Fade", desc, Type::PanSlide);
      break;
    }

    case EVENT_VIBRATO_FADE: {
      uint8_t fadeSpeed = readByte(curOffset++);
      desc = fmt::format("Fade Length: {:d}", fadeSpeed);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato Fade",
                      desc,
                      Type::Vibrato);
      break;
    }

    case EVENT_ADSR_GAIN: {
      uint8_t newADSR1 = readByte(curOffset++);
      uint8_t newADSR2 = readByte(curOffset++);
      uint8_t newGAINAmount = readByte(curOffset++);
      uint8_t newGAIN = convertGAINAmountToGAIN(newGAINAmount);

      desc = fmt::format(
          "ADSR(1): ${:02X}  ADSR(2): ${:02X}  GAIN: ${:02X}",
          newADSR1, newADSR2, newGAIN);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR(2)", desc, Type::Adsr);
      break;
    }

    case EVENT_GOTO: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      uint32_t length = curOffset - beginOffset;

      assert(dest >= dwOffset);

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
      break;
    }

    case EVENT_CALL: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;

      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pattern Play",
                      desc,
                      Type::RepeatStart);

      assert(dest >= dwOffset);

      subReturnAddr = curOffset;
      inSubroutine = true;
      curOffset = dest;
      break;
    }

    case EVENT_END: {
      if (inSubroutine) {
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "End Pattern",
                        desc,
                        Type::RepeatEnd);

        inSubroutine = false;
        curOffset = subReturnAddr;
      }
      else {
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
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

  //auto trace = fmt::format("{:08X}: {:02X} -> {:08X}", beginOffset, statusByte, curOffset);
  //LogDebug(trace.c_str());

  return bContinue;
}
