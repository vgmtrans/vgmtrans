/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "HeartBeatSnesSeq.h"
#include "ScaleConversion.h"
#include <spdlog/fmt/fmt.h>

using namespace std;

DECLARE_FORMAT(HeartBeatSnes);

//  ****************
//  HeartBeatSnesSeq
//  ****************
#define MAX_TRACKS  8
#define SEQ_PPQN    24

const uint8_t HeartBeatSnesSeq::NOTE_DUR_TABLE[16] = {
    0x23, 0x46, 0x69, 0x8c, 0xaf, 0xd2, 0xf5, 0xff,
    0x19, 0x28, 0x37, 0x46, 0x55, 0x64, 0x73, 0x82,
};

const uint8_t HeartBeatSnesSeq::NOTE_VEL_TABLE[16] = {
    0x19, 0x28, 0x37, 0x46, 0x55, 0x64, 0x73, 0x82,
    0x91, 0xa0, 0xb0, 0xbe, 0xcd, 0xdc, 0xeb, 0xff,
};

const uint8_t HeartBeatSnesSeq::PAN_TABLE[22] = {
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
    0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
    0x7a, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f,
};

HeartBeatSnesSeq::HeartBeatSnesSeq(RawFile *file,
                                   HeartBeatSnesVersion ver,
                                   uint32_t seqdataOffset,
                                   std::string name)
    : VGMSeq(HeartBeatSnesFormat::name, file, seqdataOffset, 0, std::move(name)),
      version(ver) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);

  useReverb();

  loadEventMap();
}

HeartBeatSnesSeq::~HeartBeatSnesSeq(void) {
}

void HeartBeatSnesSeq::resetVars() {
  VGMSeq::resetVars();
}

bool HeartBeatSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(dwOffset, 0);
  uint32_t curOffset = dwOffset;
  if (curOffset + 2 > 0x10000) {
    return false;
  }

  header->addChild(curOffset, 2, "Instrument Table Pointer");
  curOffset += 2;

  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t ofsTrackStart = readShort(curOffset);
    if (ofsTrackStart == 0) {
      // example: Dragon Quest 6 - Brave Fight
      header->addChild(curOffset, 2, "Track Pointer End");
      // curOffset += 2;
      break;
    }

    auto trackName = fmt::format("Track Pointer {}", trackIndex + 1);
    header->addChild(curOffset, 2, trackName);

    curOffset += 2;
  }

  return true;
}

bool HeartBeatSnesSeq::parseTrackPointers(void) {
  uint32_t curOffset = dwOffset;

  curOffset += 2;

  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t ofsTrackStart = readShort(curOffset);
    curOffset += 2;
    if (ofsTrackStart == 0) {
      break;
    }

    uint16_t addrTrackStart = dwOffset + ofsTrackStart;
    if (addrTrackStart < dwOffset) {
      return false;
    }

    HeartBeatSnesTrack *track = new HeartBeatSnesTrack(this, addrTrackStart);
    aTracks.push_back(track);
  }

  return true;
}

void HeartBeatSnesSeq::loadEventMap() {
  int statusByte;

  EventMap[0x00] = EVENT_END;

  for (statusByte = 0x01; statusByte <= 0x7f; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE_LENGTH;
  }

  for (statusByte = 0x80; statusByte <= 0xcf; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  EventMap[0xd0] = EVENT_TIE;
  EventMap[0xd1] = EVENT_REST;
  EventMap[0xd2] = EVENT_SLUR_ON;
  EventMap[0xd3] = EVENT_SLUR_OFF;
  EventMap[0xd4] = EVENT_PROGCHANGE;
  EventMap[0xd5] = EVENT_UNKNOWN0; // 7 bytes?
  EventMap[0xd6] = EVENT_PAN;
  EventMap[0xd7] = EVENT_PAN_FADE;
  EventMap[0xd8] = EVENT_VIBRATO_ON;
  EventMap[0xd9] = EVENT_VIBRATO_FADE;
  EventMap[0xda] = EVENT_VIBRATO_OFF;
  EventMap[0xdb] = EVENT_MASTER_VOLUME;
  EventMap[0xdc] = EVENT_MASTER_VOLUME_FADE;
  EventMap[0xdd] = EVENT_TEMPO;
  EventMap[0xde] = EVENT_UNKNOWN1;
  EventMap[0xdf] = EVENT_GLOBAL_TRANSPOSE;
  EventMap[0xe0] = EVENT_TRANSPOSE;
  EventMap[0xe1] = EVENT_TREMOLO_ON;
  EventMap[0xe2] = EVENT_TREMOLO_OFF;
  EventMap[0xe3] = EVENT_VOLUME;
  EventMap[0xe4] = EVENT_VOLUME_FADE;
  EventMap[0xe5] = EVENT_UNKNOWN3;
  EventMap[0xe6] = EVENT_PITCH_ENVELOPE_TO;
  EventMap[0xe7] = EVENT_PITCH_ENVELOPE_FROM;
  EventMap[0xe8] = EVENT_PITCH_ENVELOPE_OFF;
  EventMap[0xe9] = EVENT_TUNING;
  EventMap[0xea] = EVENT_ECHO_VOLUME;
  EventMap[0xeb] = EVENT_ECHO_PARAM;
  EventMap[0xec] = EVENT_UNKNOWN3;
  EventMap[0xed] = EVENT_ECHO_OFF;
  EventMap[0xee] = EVENT_ECHO_ON;
  EventMap[0xef] = EVENT_ECHO_FIR;
  EventMap[0xf0] = EVENT_ADSR;
  EventMap[0xf1] = EVENT_NOTE_PARAM;
  EventMap[0xf2] = EVENT_GOTO;
  EventMap[0xf3] = EVENT_CALL;
  EventMap[0xf4] = EVENT_RET;
  EventMap[0xf5] = EVENT_NOISE_ON;
  EventMap[0xf6] = EVENT_NOISE_OFF;
  EventMap[0xf7] = EVENT_NOISE_FREQ;
  EventMap[0xf8] = EVENT_UNKNOWN0;
  EventMap[0xf9] = EVENT_SUBEVENT;

  SubEventMap[0x00] = SUBEVENT_LOOP_COUNT;
  SubEventMap[0x01] = SUBEVENT_LOOP_AGAIN;
  SubEventMap[0x02] = SUBEVENT_UNKNOWN0;
  SubEventMap[0x03] = SUBEVENT_ADSR_AR;
  SubEventMap[0x04] = SUBEVENT_ADSR_DR;
  SubEventMap[0x05] = SUBEVENT_ADSR_SL;
  SubEventMap[0x06] = SUBEVENT_ADSR_RR;
  SubEventMap[0x07] = SUBEVENT_ADSR_SR;
  SubEventMap[0x09] = SUBEVENT_SURROUND;
}

double HeartBeatSnesSeq::getTempoInBPM(uint8_t tempo) {
  if (tempo != 0) {
    return 60000000.0 / (SEQ_PPQN * (125 * 0x10)) * (tempo / 256.0);
  }
  else {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}

//  ******************
//  HeartBeatSnesTrack
//  ******************

HeartBeatSnesTrack::HeartBeatSnesTrack(HeartBeatSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  HeartBeatSnesTrack::resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void HeartBeatSnesTrack::resetVars(void) {
  SeqTrack::resetVars();

  spcNoteDuration = 0x10;
  spcNoteDurRate = 0xaf;
  spcNoteVolume = 0xff; // just in case
  subReturnOffset = 0;
  slur = false;
}


bool HeartBeatSnesTrack::readEvent() {
  HeartBeatSnesSeq *parentSeq = static_cast<HeartBeatSnesSeq*>(this->parentSeq);

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  HeartBeatSnesSeqEventType eventType = static_cast<HeartBeatSnesSeqEventType>(0);
  std::map<uint8_t, HeartBeatSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0: {
      desc = describeUnknownEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

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

    case EVENT_END: {
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;
    }

    case EVENT_NOTE_LENGTH: {
      spcNoteDuration = statusByte;
      desc = fmt::format("Length: {:d}", spcNoteDuration);

      uint8_t noteParam = readByte(curOffset);
      if (noteParam < 0x80) {
        curOffset++;
        uint8_t durIndex = noteParam >> 4;
        uint8_t velIndex = noteParam & 0x0f;
        spcNoteDurRate = HeartBeatSnesSeq::NOTE_DUR_TABLE[durIndex];
        spcNoteVolume = HeartBeatSnesSeq::NOTE_VEL_TABLE[velIndex];
        desc += fmt::format("  Duration: {:d} ({:d})  Velocity: {:d} ({:d})",
          durIndex, spcNoteDurRate, velIndex, spcNoteVolume);
      }

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Note Length & Param",
                      desc,
                      Type::ChangeState);
      break;
    }

    case EVENT_NOTE: {
      uint8_t key = statusByte - 0x80;

      uint8_t dur;
      if (slur) {
        // full-length
        dur = spcNoteDuration;
      }
      else {
        dur = max((spcNoteDuration * spcNoteDurRate) >> 8, 0);
        dur = (dur > 2) ? dur - 1 : 1;
      }

      addNoteByDur(beginOffset, curOffset - beginOffset, key, spcNoteVolume / 2, dur, "Note");
      addTime(spcNoteDuration);
      break;
    }

    case EVENT_TIE: {
      uint8_t dur;
      if (slur) {
        // full-length
        dur = spcNoteDuration;
      }
      else {
        dur = max((spcNoteDuration * spcNoteDurRate) >> 8, 0);
        dur = (dur > 2) ? dur - 1 : 1;
      }

      desc = fmt::format("Duration: {:d}", dur);
      makePrevDurNoteEnd(getTime() + dur);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc, Type::Tie);
      addTime(spcNoteDuration);
      break;
    }

    case EVENT_REST: {
      addRest(beginOffset, curOffset - beginOffset, spcNoteDuration);
      break;
    }

    // Is this Slur or Portamento?
    case EVENT_SLUR_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Slur On",
                      desc,
                      Type::Portamento);
      slur = true;
      break;
    }

    case EVENT_SLUR_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Slur Off",
                      desc,
                      Type::Portamento);
      slur = false;
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t newProgNum = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, newProgNum, true);
      break;
    }

    case EVENT_PAN: {
      uint8_t newPan = readByte(curOffset++);

      uint8_t panIndex = std::min<uint8_t>(newPan & 0x1fu, 20u);

      uint8_t volumeLeft = HeartBeatSnesSeq::PAN_TABLE[20 - panIndex];
      uint8_t volumeRight = HeartBeatSnesSeq::PAN_TABLE[panIndex];

      double linearPan = static_cast<double>(volumeRight) / (volumeLeft + volumeRight);
      uint8_t midiPan = convertLinearPercentPanValToStdMidiVal(linearPan);

      // TODO: apply volume scale
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      break;
    }

    case EVENT_PAN_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      uint8_t newPan = readByte(curOffset++);

      uint8_t panIndex = min<uint8_t>((newPan & 0x1Fu), 20u);

      double volumeLeft = HeartBeatSnesSeq::PAN_TABLE[20 - panIndex] / 128.0;
      double volumeRight = HeartBeatSnesSeq::PAN_TABLE[panIndex] / 128.0;

      double linearPan = (double) volumeRight / (volumeLeft + volumeRight);
      uint8_t midiPan = convertLinearPercentPanValToStdMidiVal(linearPan);

      // TODO: fade in real curve, apply volume scale
      addPanSlide(beginOffset, curOffset - beginOffset, fadeLength, midiPan);
      break;
    }

    case EVENT_VIBRATO_ON: {
      uint8_t vibratoDelay = readByte(curOffset++);
      uint8_t vibratoRate = readByte(curOffset++);
      uint8_t vibratoDepth = readByte(curOffset++);

      desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}", vibratoDelay, vibratoRate, vibratoDepth);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato",
                      desc,
                      Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      desc = fmt::format("Length: {:d}", fadeLength);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato Fade",
                      desc,
                      Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato Off",
                      desc,
                      Type::Vibrato);
      break;
    }

    case EVENT_MASTER_VOLUME: {
      uint8_t newVol = readByte(curOffset++);
      addMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
      break;
    }

    case EVENT_MASTER_VOLUME_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      uint8_t newVol = readByte(curOffset++);

      // desc = fmt::format("Length: {:d}  Volume: {:d}", fadeLength, newVol);
      addMastVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = readByte(curOffset++);
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM(newTempo));
      break;
    }

    case EVENT_GLOBAL_TRANSPOSE: {
      int8_t semitones = readByte(curOffset++);
      addGlobalTranspose(beginOffset, curOffset - beginOffset, semitones);
      break;
    }

    case EVENT_TRANSPOSE: {
      int8_t semitones = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, semitones);
      break;
    }

    case EVENT_TREMOLO_ON: {
      uint8_t tremoloDelay = readByte(curOffset++);
      uint8_t tremoloRate = readByte(curOffset++);
      uint8_t tremoloDepth = readByte(curOffset++);

      desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}", tremoloDelay, tremoloRate, tremoloDepth);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tremolo",
                      desc,
                      Type::Tremelo);
      break;
    }

    case EVENT_TREMOLO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tremolo Off",
                      desc,
                      Type::Tremelo);
      break;
    }

    case EVENT_VOLUME: {
      uint8_t newVol = readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, newVol / 2);
      break;
    }

    case EVENT_VOLUME_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      uint8_t newVol = readByte(curOffset++);
      addVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
      break;
    }

    case EVENT_PITCH_ENVELOPE_TO: {
      uint8_t pitchEnvDelay = readByte(curOffset++);
      uint8_t pitchEnvLength = readByte(curOffset++);
      int8_t pitchEnvSemitones = static_cast<int8_t>(readByte(curOffset++));

      desc = fmt::format("Delay: {:d}  Length: {:d}  Semitones: {:d}", pitchEnvDelay, pitchEnvLength, pitchEnvSemitones);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Envelope (To)",
                      desc,
                      Type::PitchBendSlide);
      break;
    }

    case EVENT_PITCH_ENVELOPE_FROM: {
      uint8_t pitchEnvDelay = readByte(curOffset++);
      uint8_t pitchEnvLength = readByte(curOffset++);
      int8_t pitchEnvSemitones = static_cast<int8_t>(readByte(curOffset++));

      desc = fmt::format("Delay: {:d}  Length: {:d}  Semitones: {:d}", pitchEnvDelay, pitchEnvLength, pitchEnvSemitones);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Envelope (From)",
                      desc,
                      Type::PitchBendSlide);
      break;
    }

    case EVENT_PITCH_ENVELOPE_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Envelope Off",
                      desc,
                      Type::PitchBend);
      break;
    }

    case EVENT_TUNING: {
      uint8_t newTuning = readByte(curOffset++);
      addFineTuning(beginOffset, curOffset - beginOffset, (newTuning / 256.0) * 100.0);
      break;
    }

    case EVENT_ECHO_VOLUME: {
      uint8_t spcEVOL_L = readByte(curOffset++);
      uint8_t spcEVOL_R = readByte(curOffset++);
      desc = fmt::format("Volume Left: {:d}  Volume Right: {:d}", spcEVOL_L, spcEVOL_R);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Volume",
                      desc,
                      Type::Reverb);
      break;
    }

    case EVENT_ECHO_PARAM: {
      uint8_t spcEDL = readByte(curOffset++);
      uint8_t spcEFB = readByte(curOffset++);
      uint8_t spcFIR = readByte(curOffset++);

      desc = fmt::format("Delay: {:d}  Feedback: {:d}  FIR: {:d}", spcEDL, spcEFB, spcFIR);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Off", desc, Type::Reverb);
      break;
    }

    case EVENT_ECHO_OFF: {
      addReverb(beginOffset, curOffset - beginOffset, 0, "Echo Off");
      break;
    }

    case EVENT_ECHO_ON: {
      addReverb(beginOffset, curOffset - beginOffset, 100, "Echo On");
      break;
    }

    case EVENT_ECHO_FIR: {
      curOffset += 8; // FIR C0-C7
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo FIR", desc, Type::Reverb);
      break;
    }

    case EVENT_ADSR: {
      uint8_t adsr1 = readByte(curOffset++);
      uint8_t adsr2 = readByte(curOffset++);

      uint8_t ar = adsr1 & 0x0f;
      uint8_t dr = (adsr1 & 0x70) >> 4;
      uint8_t sl = (adsr2 & 0xe0) >> 5;
      uint8_t sr = adsr2 & 0x1f;

      desc = fmt::format("AR: {:d}  DR: {:d}  SL: {:d}  SR: {:d}", ar, dr, sl, sr);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      break;
    }

    case EVENT_NOTE_PARAM: {
      uint8_t noteParam = readByte(curOffset++);
      uint8_t durIndex = noteParam >> 4;
      uint8_t velIndex = noteParam & 0x0f;
      spcNoteDurRate = HeartBeatSnesSeq::NOTE_DUR_TABLE[durIndex];
      spcNoteVolume = HeartBeatSnesSeq::NOTE_VEL_TABLE[velIndex];
      desc += fmt::format("  Duration: {:d} ({:d})  Velocity: {:d} ({:d})",
          durIndex, spcNoteDurRate, velIndex, spcNoteVolume);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::ChangeState);
      break;
    }

    case EVENT_GOTO: {
      int16_t destOffset = readShort(curOffset);
      curOffset += 2;
      uint16_t dest = parentSeq->dwOffset + destOffset;
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

    case EVENT_CALL: {
      int16_t destOffset = readShort(curOffset);
      curOffset += 2;
      uint16_t dest = parentSeq->dwOffset + destOffset;
      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pattern Play",
                      desc,
                      Type::RepeatStart);

      subReturnOffset = curOffset - parentSeq->dwOffset;

      curOffset = dest;
      break;
    }

    case EVENT_RET: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern End", desc, Type::RepeatEnd);
      curOffset = parentSeq->dwOffset + subReturnOffset;
      break;
    }

    case EVENT_NOISE_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Noise On",
                      desc,
                      Type::Noise);
      break;
    }

    case EVENT_NOISE_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Noise Off",
                      desc,
                      Type::Noise);
      break;
    }

    case EVENT_NOISE_FREQ: {
      uint8_t newNCK = readByte(curOffset++) & 0x1f;
      desc = fmt::format("Noise Frequency (NCK): {:d}", newNCK);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Noise Frequency",
                      desc,
                      Type::Noise);
      break;
    }

    case EVENT_SUBEVENT: {
      uint8_t subStatusByte = readByte(curOffset++);
      HeartBeatSnesSeqSubEventType subEventType = static_cast<HeartBeatSnesSeqSubEventType>(0);
      std::map<uint8_t, HeartBeatSnesSeqSubEventType>::iterator
          pSubEventType = parentSeq->SubEventMap.find(subStatusByte);
      if (pSubEventType != parentSeq->SubEventMap.end()) {
        subEventType = pSubEventType->second;
      }

      switch (subEventType) {
        case SUBEVENT_UNKNOWN0:
          desc = logEvent(subStatusByte, spdlog::level::off, "Subevent");
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
          break;

        case SUBEVENT_UNKNOWN1: {
          uint8_t arg1 = readByte(curOffset++);
          desc = logEvent(subStatusByte, spdlog::level::off, "Subevent", arg1);
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
          break;
        }

        case SUBEVENT_UNKNOWN2: {
          uint8_t arg1 = readByte(curOffset++);
          uint8_t arg2 = readByte(curOffset++);
          desc = logEvent(subStatusByte, spdlog::level::off, "Subevent", arg1, arg2);
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
          break;
        }

        case SUBEVENT_UNKNOWN3: {
          uint8_t arg1 = readByte(curOffset++);
          uint8_t arg2 = readByte(curOffset++);
          uint8_t arg3 = readByte(curOffset++);
          desc = logEvent(subStatusByte, spdlog::level::off, "Subevent", arg1, arg2, arg3);
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
          break;
        }

        case SUBEVENT_UNKNOWN4: {
          uint8_t arg1 = readByte(curOffset++);
          uint8_t arg2 = readByte(curOffset++);
          uint8_t arg3 = readByte(curOffset++);
          uint8_t arg4 = readByte(curOffset++);
          desc = logEvent(subStatusByte, spdlog::level::off, "Subevent", arg1, arg2, arg3, arg4);
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
          break;
        }

        case SUBEVENT_LOOP_COUNT: {
          loopCount = readByte(curOffset++);
          desc = fmt::format("Times: {:d}", loopCount);
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "Loop Count",
                          desc,
                          Type::Loop);
          break;
        }

        case SUBEVENT_LOOP_AGAIN: {
          int16_t destOffset = readShort(curOffset);
          curOffset += 2;
          uint16_t dest = parentSeq->dwOffset + destOffset;
          desc = fmt::format("Destination: ${:04X}", dest);
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "Loop Again",
                          desc,
                          Type::Loop);

          if (loopCount != 0) {
            loopCount--;
            if (loopCount != 0) {
              curOffset = dest;
            }
          }
          break;
        }

        case SUBEVENT_ADSR_AR: {
          uint8_t newAR = readByte(curOffset++) & 15;
          desc = fmt::format("AR: {:d}", newAR);
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "ADSR Attack Rate",
                          desc,
                          Type::Adsr);
          break;
        }

        case SUBEVENT_ADSR_DR: {
          uint8_t newDR = readByte(curOffset++) & 7;
          desc = fmt::format("DR: {:d}", newDR);
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "ADSR Decay Rate",
                          desc,
                          Type::Adsr);
          break;
        }

        case SUBEVENT_ADSR_SL: {
          uint8_t newSL = readByte(curOffset++) & 7;
          desc = fmt::format("SL: {:d}", newSL);
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "ADSR Sustain Level",
                          desc,
                          Type::Adsr);
          break;
        }

        case SUBEVENT_ADSR_RR: {
          uint8_t newRR = readByte(curOffset++) & 15;
          desc = fmt::format("RR: {:d}", newRR);
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "ADSR Release Rate",
                          desc,
                          Type::Adsr);
          break;
        }

        case SUBEVENT_ADSR_SR: {
          uint8_t newSR = readByte(curOffset++) & 15;
          desc = fmt::format("SR: {:d}", newSR);
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "ADSR Sustain Rate",
                          desc,
                          Type::Adsr);
          break;
        }

        case SUBEVENT_SURROUND: {
          bool invertLeft = readByte(curOffset++) != 0;
          bool invertRight = readByte(curOffset++) != 0;
          desc = fmt::format("Invert Left: {}  Invert Right: {}",
                             invertLeft ? "On" : "Off", invertRight ? "On" : "Off");
          addGenericEvent(beginOffset, curOffset - beginOffset, "Surround", desc, Type::Pan);
          break;
        }

        default: {
          auto descr = logEvent(subStatusByte, spdlog::level::err, "Subevent");
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
          bContinue = false;
          break;
        }
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

  //ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}
