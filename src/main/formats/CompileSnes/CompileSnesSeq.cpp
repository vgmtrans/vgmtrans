/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "CompileSnesSeq.h"

#include "base/Types.h"
#include "ScaleConversion.h"

#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(CompileSnes);

//  **************
//  CompileSnesSeq
//  **************
#define MAX_TRACKS  8
#define SEQ_PPQN    12
#define SEQ_KEYOFS  12

#define COMPILESNES_FLAGS_PORTAMENTO    0x10

// duration table
const u8 CompileSnesSeq::noteDurTable[] = {
    0x01, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0c, 0x10,
    0x18, 0x20, 0x30, 0x09, 0x12, 0x1e, 0x24, 0x2a,
};

CompileSnesSeq::CompileSnesSeq(RawFile *file, CompileSnesVersion ver, u32 seqdataOffset, std::string name)
    : VGMSeq(CompileSnesFormat::name, file, seqdataOffset, 0, std::move(name)), version(ver),
      STATUS_PERCUSSION_NOTE_MIN(0xc0),
      STATUS_PERCUSSION_NOTE_MAX(0xdd),
      STATUS_DURATION_DIRECT(0xde),
      STATUS_DURATION_MIN(0xdf),
      STATUS_DURATION_MAX(0xee) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);

  loadEventMap();
}

CompileSnesSeq::~CompileSnesSeq() {
}

void CompileSnesSeq::resetVars() {
  VGMSeq::resetVars();
}

bool CompileSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(offset(), 0);

  header->addChild(offset(), 1, "Number of Tracks");
  nNumTracks = readByte(offset());
  if (nNumTracks == 0 || nNumTracks > 8) {
    return false;
  }

  u32 curOffset = offset() + 1;
  for (u8 trackIndex = 0; trackIndex < nNumTracks; trackIndex++) {
    auto trackName = fmt::format("Track {}", trackIndex + 1);

    VGMHeader *trackHeader = header->addHeader(curOffset, 14, trackName);
    trackHeader->addChild(curOffset, 1, "Channel");
    trackHeader->addChild(curOffset + 1, 1, "Flags");
    trackHeader->addChild(curOffset + 2, 1, "Volume");
    trackHeader->addChild(curOffset + 3, 1, "Volume Envelope");
    trackHeader->addChild(curOffset + 4, 1, "Vibrato");
    trackHeader->addChild(curOffset + 5, 1, "Transpose");
    trackHeader->addTempo(curOffset + 6, 1);
    trackHeader->addChild(curOffset + 7, 1, "Branch ID (Channel #)");
    trackHeader->addChild(curOffset + 8, 2, "Score Pointer");
    trackHeader->addChild(curOffset + 10, 1, "SRCN");
    trackHeader->addChild(curOffset + 11, 1, "ADSR");
    trackHeader->addChild(curOffset + 12, 1, "Pan");
    trackHeader->addChild(curOffset + 13, 1, "Reserved");
    curOffset += 14;
  }

  return true;        //successful
}


bool CompileSnesSeq::parseTrackPointers(void) {
  u32 curOffset = offset() + 1;
  for (u8 trackIndex = 0; trackIndex < nNumTracks; trackIndex++) {
    u16 ofsTrackStart = readShort(curOffset + 8);

    CompileSnesTrack *track = addTrack<CompileSnesTrack>(this, ofsTrackStart);
    track->spcInitialFlags = readByte(curOffset + 1);
    track->spcInitialVolume = readByte(curOffset + 2);
    track->spcInitialTranspose = static_cast<s8>(readByte(curOffset + 5));
    track->spcInitialTempo = readByte(curOffset + 6);
    track->spcInitialSRCN = readByte(curOffset + 10);
    track->spcInitialPan = static_cast<s8>(readByte(curOffset + 12));
    if (trackIndex == 0) {
      setAlwaysWriteInitialTempo(getTempoInBPM(track->spcInitialTempo));
    }

    curOffset += 14;
  }

  return true;
}

void CompileSnesSeq::loadEventMap() {
  for (unsigned int statusByte = 0x00; statusByte <= 0x7f; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  EventMap[0x80] = EVENT_GOTO;
  EventMap[0x81] = EVENT_LOOP_END;
  EventMap[0x82] = EVENT_END;
  EventMap[0x83] = EVENT_VIBRATO;
  EventMap[0x84] = EVENT_PORTAMENTO_TIME;
  //EventMap[0x85] = 0;
  //EventMap[0x86] = 0;
  EventMap[0x87] = EVENT_VOLUME;
  EventMap[0x88] = EVENT_VOLUME_ENVELOPE;
  EventMap[0x89] = EVENT_TRANSPOSE;
  EventMap[0x8a] = EVENT_VOLUME_REL;
  EventMap[0x8b] = EVENT_UNKNOWN2;
  EventMap[0x8c] = EVENT_UNKNOWN1; // NOP
  EventMap[0x8d] = EVENT_LOOP_COUNT;
  EventMap[0x8e] = EVENT_UNKNOWN1;
  EventMap[0x8f] = EVENT_UNKNOWN1;
  EventMap[0x90] = EVENT_FLAGS;
  EventMap[0x91] = EVENT_UNKNOWN1;
  EventMap[0x92] = EVENT_UNKNOWN1;
  EventMap[0x93] = EVENT_UNKNOWN2;
  EventMap[0x94] = EVENT_UNKNOWN1;
  // 95 no version differences
  EventMap[0x96] = EVENT_TEMPO;
  EventMap[0x97] = EVENT_TUNING;
  EventMap[0x98] = EVENT_UNKNOWN1;
  EventMap[0x99] = EVENT_UNKNOWN0;
  EventMap[0x9a] = EVENT_CALL;
  EventMap[0x9b] = EVENT_RET;
  //EventMap[0x9c] = 0;
  EventMap[0x9d] = EVENT_UNKNOWN1;
  //EventMap[0x9e] = 0;
  EventMap[0x9f] = EVENT_ADSR;
  EventMap[0xa0] = EVENT_PROGCHANGE;
  EventMap[0xa1] = EVENT_PORTAMENTO_ON;
  EventMap[0xa2] = EVENT_PORTAMENTO_OFF;
  EventMap[0xa3] = EVENT_PANPOT_ENVELOPE;
  EventMap[0xa4] = EVENT_UNKNOWN1; // conditional do (channel match), for delay
  EventMap[0xa5] = EVENT_UNKNOWN3; // conditional jump
  EventMap[0xa6] = EVENT_UNKNOWN1;
  EventMap[0xa7] = EVENT_UNKNOWN1;
  EventMap[0xab] = EVENT_PAN;
  EventMap[0xac] = EVENT_UNKNOWN1;
  EventMap[0xad] = EVENT_LOOP_BREAK;
  EventMap[0xae] = EVENT_UNKNOWN0;
  EventMap[0xaf] = EVENT_UNKNOWN0;

  for (unsigned int statusByte = STATUS_PERCUSSION_NOTE_MIN; statusByte <= STATUS_PERCUSSION_NOTE_MAX; statusByte++) {
    EventMap[statusByte] = EVENT_PERCUSSION_NOTE;
  }

  EventMap[STATUS_DURATION_DIRECT] = EVENT_DURATION_DIRECT;
  for (unsigned int statusByte = STATUS_DURATION_MIN; statusByte <= STATUS_DURATION_MAX; statusByte++) {
    EventMap[statusByte] = EVENT_DURATION;
  }

  switch (version) {
    case COMPILESNES_ALESTE:
      EventMap[0xa4] = EVENT_UNKNOWN1;
      EventMap[0xa5] = EVENT_UNKNOWN1;
      EventMap[0xa6] = EVENT_UNKNOWN1;
      break;

    default:
      break;
  }
}

double CompileSnesSeq::getTempoInBPM(u8 tempo) {
  // cite: <http://www6.atpages.jp/appsouko/work/TAS/doc/fps.html>
  constexpr double SNES_NTSC_FRAMERATE = 39375000.0 / 655171.0;

  unsigned int tempoValue = (tempo == 0) ? 256 : tempo;
  return 60000000.0 / (SEQ_PPQN * (1000000.0 / SNES_NTSC_FRAMERATE)) * (tempoValue / 256.0);
}


//  ****************
//  CompileSnesTrack
//  ****************

CompileSnesTrack::CompileSnesTrack(CompileSnesSeq *parentFile, u32 offset, u32 length)
    : SeqTrack(parentFile, offset, length) {
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void CompileSnesTrack::resetVars() {
  SeqTrack::resetVars();

  cKeyCorrection = SEQ_KEYOFS;

  spcNoteDuration = 1;
  spcFlags = spcInitialFlags;
  spcVolume = spcInitialVolume;
  spcTranspose = spcInitialTranspose;
  spcTempo = spcInitialTempo;
  spcSRCN = spcInitialSRCN;
  spcPan = spcInitialPan;
  memset(repeatCount, 0, sizeof(repeatCount));

  transpose = spcTranspose;
}

void CompileSnesTrack::addInitialMidiEvents(int trackNum) {
  SeqTrack::addInitialMidiEvents(trackNum);

  double volumeScale;
  addProgramChangeNoItem(spcSRCN, true);
  addVolNoItem(convert7bitPercentAmpToStdMidiVal(spcVolume / 2));
  addPanNoItem(convert7bitLinearPercentPanValToStdMidiVal(static_cast<u8>(spcPan + 0x80) / 2, &volumeScale));
  addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));
  addReverbNoItem(0);
}

bool CompileSnesTrack::readEvent() {
  CompileSnesSeq *parentSeq = static_cast<CompileSnesSeq*>(this->parentSeq);

  u32 beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  u8 statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  CompileSnesSeqEventType eventType = static_cast<CompileSnesSeqEventType>(0);
  std::map<u8, CompileSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

    case EVENT_UNKNOWN5: {
      u8 arg1 = readByte(curOffset++);
      u8 arg2 = readByte(curOffset++);
      u8 arg3 = readByte(curOffset++);
      u8 arg4 = readByte(curOffset++);
      u8 arg5 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3, arg4, arg5);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_GOTO: {
      u16 dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      u32 length = curOffset - beginOffset;

      curOffset = dest;
      if (!isOffsetUsed(dest)) {
        addGenericEvent(beginOffset, length, "Jump", desc, Type::LoopForever);
      }
      else {
        bContinue = addLoopForever(beginOffset, length, "Jump");
      }
      break;
    }

    case EVENT_LOOP_END: {
      u8 repeatNest = readByte(curOffset++);
      u16 dest = readShort(curOffset);
      curOffset += 2;

      desc = fmt::format("Nest Level: {:d}  Destination: ${:04X}", repeatNest, dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat End", desc, Type::RepeatEnd);

      repeatCount[repeatNest]--;
      if (repeatCount[repeatNest] != 0) {
        curOffset = dest;
      }
      break;
    }

    case EVENT_END: {
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;
    }

    case EVENT_VIBRATO: {
      u8 envelopeIndex = readByte(curOffset++);
      desc = fmt::format("Envelope Index: {:d}", envelopeIndex);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato",
                      desc,
                      Type::Vibrato);
      break;
    }

    case EVENT_PORTAMENTO_TIME: {
      u8 rate = readByte(curOffset++);
      desc = fmt::format("Rate: {:d}", rate);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Portamento Time",
                      desc,
                      Type::PortamentoTime);
      break;
    }

    case EVENT_VOLUME: {
      u8 newVolume = readByte(curOffset++);
      spcVolume = newVolume;
      u8 midiVolume = convert7bitPercentAmpToStdMidiVal(spcVolume / 2);
      addVol(beginOffset, curOffset - beginOffset, midiVolume);
      break;
    }

    case EVENT_VOLUME_ENVELOPE: {
      u8 envelopeIndex = readByte(curOffset++);
      desc = fmt::format("Envelope Index: {:d}", envelopeIndex);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Volume Envelope",
                      desc,
                      Type::VolumeEnvelope);
      break;
    }

    case EVENT_TRANSPOSE: {
      s8 delta = static_cast<s8>(readByte(curOffset++));
      spcTranspose += delta;
      addTranspose(beginOffset, curOffset - beginOffset, spcTranspose, "Transpose (Relative)");
      break;
    }

    case EVENT_VOLUME_REL: {
      s8 delta = static_cast<s8>(readByte(curOffset++));
      spcVolume += delta;
      u8 midiVolume = convert7bitPercentAmpToStdMidiVal(spcVolume / 2);
      addVol(beginOffset, curOffset - beginOffset, midiVolume, "Volume (Relative)");
      break;
    }

    case EVENT_NOTE: {
      bool rest = (statusByte == 0x00);

      u8 duration;
      bool hasDuration = readDurationBytes(curOffset, duration);
      if (hasDuration) {
        spcNoteDuration = duration;
      }

      if (rest) {
        addRest(beginOffset, curOffset - beginOffset, spcNoteDuration);
      }
      else {
        u8 noteNumber = statusByte - 1;
        addNoteByDur(beginOffset, curOffset - beginOffset, noteNumber, 100, spcNoteDuration,
                     hasDuration ? "Note with Duration" : "Note");
        addTime(spcNoteDuration);
      }
      break;
    }

    case EVENT_LOOP_COUNT: {
      u8 repeatNest = readByte(curOffset++);
      u8 times = readByte(curOffset++);
      int actualTimes = (times == 0) ? 256 : times;

      desc = fmt::format("Nest Level: {:d}  Times: {:d}", repeatNest, actualTimes);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Count", desc, Type::RepeatStart);

      repeatCount[repeatNest] = times;
      break;
    }

    case EVENT_FLAGS: {
      u8 flags = readByte(curOffset++);
      spcFlags = flags;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Flags",
                      desc,
                      Type::ChangeState);
      break;
    }

    case EVENT_TEMPO: {
      u8 newTempo = readByte(curOffset++);
      spcTempo = newTempo;
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM(newTempo));
      break;
    }

    case EVENT_TUNING: {
      s16 newTuning;
      if (parentSeq->version == COMPILESNES_ALESTE || parentSeq->version == COMPILESNES_JAKICRUSH) {
        newTuning = static_cast<s8>(readByte(curOffset++));
      }
      else {
        newTuning = static_cast<s16>(readShort(curOffset));
        curOffset += 2;
      }

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tuning",
                      fmt::format("Pitch Register Delta: {:d}", newTuning),
                      Type::FineTune);
      break;
    }

    case EVENT_CALL: {
      u16 dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pattern Play",
                      desc,
                      Type::RepeatStart);

      subReturnAddress = curOffset;
      curOffset = dest;
      break;
    }

    case EVENT_RET: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "End Pattern",
                      desc,
                      Type::RepeatEnd);
      curOffset = subReturnAddress;
      break;
    }

    case EVENT_PROGCHANGE: {
      u8 newProg = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, newProg, true);
      break;
    }

    case EVENT_ADSR: {
      u8 envelopeIndex = readByte(curOffset++);
      desc = fmt::format("Envelope Index: {:d}", envelopeIndex);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      break;
    }

    case EVENT_PORTAMENTO_ON: {
      spcFlags |= COMPILESNES_FLAGS_PORTAMENTO;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Portamento On",
                      desc,
                      Type::Portamento);
      break;
    }

    case EVENT_PORTAMENTO_OFF: {
      spcFlags &= ~COMPILESNES_FLAGS_PORTAMENTO;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Portamento Off",
                      desc,
                      Type::Portamento);
      break;
    }

    case EVENT_PANPOT_ENVELOPE: {
      u8 envelopeIndex = readByte(curOffset++);
      desc = fmt::format("Envelope Index: {:d}", envelopeIndex);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Panpot Envelope",
                      desc,
                      Type::PanEnvelope);
      break;
    }

    case EVENT_PAN: {
      s8 newPan = readByte(curOffset++);
      spcPan = newPan;

      double volumeScale;
      u8 midiPan = convert7bitLinearPercentPanValToStdMidiVal(static_cast<u8>(newPan + 0x80) / 2, &volumeScale);
      addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      break;
    }

    case EVENT_LOOP_BREAK: {
      u8 repeatNest = readByte(curOffset++);
      u16 dest = readShort(curOffset);
      curOffset += 2;

      desc = fmt::format("Nest Level: {:d}  Destination: ${:04X}", repeatNest, dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Break", desc, Type::LoopBreak);

      repeatCount[repeatNest]--;
      if (repeatCount[repeatNest] == 0) {
        curOffset = dest;
      }
      break;
    }

    case EVENT_DURATION_DIRECT: {
      u8 duration;
      if (!readDurationBytes(curOffset, duration)) {
        // address out of range
        return false;
      }
      spcNoteDuration = duration;

      desc = fmt::format("Duration: {:d}", duration);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Duration (Direct)",
                      desc,
                      Type::DurationChange);
      break;
    }

    case EVENT_DURATION: {
      u8 duration;
      if (!readDurationBytes(curOffset, duration)) {
        // address out of range
        return false;
      }
      spcNoteDuration = duration;

      desc = fmt::format("Duration: {:d}", duration);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration", desc, Type::DurationChange);
      break;
    }

    case EVENT_PERCUSSION_NOTE: {
      u8 duration;
      bool hasDuration = readDurationBytes(curOffset, duration);
      if (hasDuration) {
        spcNoteDuration = duration;
      }

      u8 percNoteNumber = statusByte - parentSeq->STATUS_PERCUSSION_NOTE_MIN;
      addPercNoteByDur(beginOffset, curOffset - beginOffset, percNoteNumber, 100, spcNoteDuration,
                       hasDuration ? "Percussion Note with Duration" : "Percussion Note");
      addTime(spcNoteDuration);
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

bool CompileSnesTrack::readDurationBytes(u32& offset, u8& duration) const {
  CompileSnesSeq *parentSeq = static_cast<CompileSnesSeq*>(this->parentSeq);

  u32 curOffset = offset;
  bool durationDispatched = false;
  while (curOffset < 0x10000) {
    u8 statusByte = readByte(curOffset++);

    if (statusByte == parentSeq->STATUS_DURATION_DIRECT) {
      if (curOffset >= 0x10000) {
        break;
      }

      duration = readByte(curOffset++);
      offset = curOffset;
      durationDispatched = true;
    }
    else if (statusByte >= parentSeq->STATUS_DURATION_MIN && statusByte <= parentSeq->STATUS_DURATION_MAX) {
      duration = CompileSnesSeq::noteDurTable[statusByte - parentSeq->STATUS_DURATION_MIN];
      offset = curOffset;
      durationDispatched = true;
    }
    else {
      break;
    }
  }
  return durationDispatched;
}
