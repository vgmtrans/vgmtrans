/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "CompileSnesSeq.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(CompileSnes);

//  **************
//  CompileSnesSeq
//  **************
#define MAX_TRACKS  8
#define SEQ_PPQN    12
#define SEQ_KEYOFS  12

#define COMPILESNES_FLAGS_PORTAMENTO    0x10

// duration table
const uint8_t CompileSnesSeq::noteDurTable[] = {
    0x01, 0x02, 0x03, 0x04, 0x06, 0x08, 0x0c, 0x10,
    0x18, 0x20, 0x30, 0x09, 0x12, 0x1e, 0x24, 0x2a,
};

CompileSnesSeq::CompileSnesSeq(RawFile *file, CompileSnesVersion ver, uint32_t seqdataOffset, std::string name)
    : VGMSeq(CompileSnesFormat::name, file, seqdataOffset, 0, std::move(name)), version(ver),
      STATUS_PERCUSSION_NOTE_MIN(0xc0),
      STATUS_PERCUSSION_NOTE_MAX(0xdd),
      STATUS_DURATION_DIRECT(0xde),
      STATUS_DURATION_MIN(0xdf),
      STATUS_DURATION_MAX(0xee) {
  bLoadTickByTick = true;
  bAllowDiscontinuousTrackData = true;

  LoadEventMap();
}

CompileSnesSeq::~CompileSnesSeq() {
}

void CompileSnesSeq::ResetVars() {
  VGMSeq::ResetVars();
}

bool CompileSnesSeq::GetHeaderInfo() {
  SetPPQN(SEQ_PPQN);

  VGMHeader *header = AddHeader(dwOffset, 0);

  header->AddSimpleItem(dwOffset, 1, "Number of Tracks");
  nNumTracks = GetByte(dwOffset);
  if (nNumTracks == 0 || nNumTracks > 8) {
    return false;
  }

  uint32_t curOffset = dwOffset + 1;
  for (uint8_t trackIndex = 0; trackIndex < nNumTracks; trackIndex++) {
    std::stringstream trackName;
    trackName << "Track " << (trackIndex + 1);

    VGMHeader *trackHeader = header->AddHeader(curOffset, 14, trackName.str().c_str());
    trackHeader->AddSimpleItem(curOffset, 1, "Channel");
    trackHeader->AddSimpleItem(curOffset + 1, 1, "Flags");
    trackHeader->AddSimpleItem(curOffset + 2, 1, "Volume");
    trackHeader->AddSimpleItem(curOffset + 3, 1, "Volume Envelope");
    trackHeader->AddSimpleItem(curOffset + 4, 1, "Vibrato");
    trackHeader->AddSimpleItem(curOffset + 5, 1, "Transpose");
    trackHeader->AddTempo(curOffset + 6, 1);
    trackHeader->AddSimpleItem(curOffset + 7, 1, "Branch ID (Channel #)");
    trackHeader->AddSimpleItem(curOffset + 8, 2, "Score Pointer");
    trackHeader->AddSimpleItem(curOffset + 10, 1, "SRCN");
    trackHeader->AddSimpleItem(curOffset + 11, 1, "ADSR");
    trackHeader->AddSimpleItem(curOffset + 12, 1, "Pan");
    trackHeader->AddSimpleItem(curOffset + 13, 1, "Reserved");
    curOffset += 14;
  }

  return true;        //successful
}


bool CompileSnesSeq::GetTrackPointers(void) {
  uint32_t curOffset = dwOffset + 1;
  for (uint8_t trackIndex = 0; trackIndex < nNumTracks; trackIndex++) {
    uint16_t ofsTrackStart = GetShort(curOffset + 8);

    CompileSnesTrack *track = new CompileSnesTrack(this, ofsTrackStart);
    track->spcInitialFlags = GetByte(curOffset + 1);
    track->spcInitialVolume = GetByte(curOffset + 2);
    track->spcInitialTranspose = static_cast<int8_t>(GetByte(curOffset + 5));
    track->spcInitialTempo = GetByte(curOffset + 6);
    track->spcInitialSRCN = GetByte(curOffset + 10);
    track->spcInitialPan = static_cast<int8_t>(GetByte(curOffset + 12));
    aTracks.push_back(track);

    if (trackIndex == 0) {
      AlwaysWriteInitialTempo(GetTempoInBPM(track->spcInitialTempo));
    }

    curOffset += 14;
  }

  return true;
}

void CompileSnesSeq::LoadEventMap() {
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

double CompileSnesSeq::GetTempoInBPM(uint8_t tempo) {
  // cite: <http://www6.atpages.jp/appsouko/work/TAS/doc/fps.html>
  constexpr double SNES_NTSC_FRAMERATE = 39375000.0 / 655171.0;

  unsigned int tempoValue = (tempo == 0) ? 256 : tempo;
  return 60000000.0 / (SEQ_PPQN * (1000000.0 / SNES_NTSC_FRAMERATE)) * (tempoValue / 256.0);
}


//  ****************
//  CompileSnesTrack
//  ****************

CompileSnesTrack::CompileSnesTrack(CompileSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void CompileSnesTrack::ResetVars() {
  SeqTrack::ResetVars();

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

void CompileSnesTrack::AddInitialMidiEvents(int trackNum) {
  SeqTrack::AddInitialMidiEvents(trackNum);

  double volumeScale;
  AddProgramChangeNoItem(spcSRCN, true);
  AddVolNoItem(Convert7bitPercentVolValToStdMidiVal(spcVolume / 2));
  AddPanNoItem(Convert7bitLinearPercentPanValToStdMidiVal(static_cast<uint8_t>(spcPan + 0x80) / 2, &volumeScale));
  AddExpressionNoItem(ConvertPercentAmpToStdMidiVal(volumeScale));
  AddReverbNoItem(0);
}

bool CompileSnesTrack::ReadEvent() {
  CompileSnesSeq *parentSeq = static_cast<CompileSnesSeq*>(this->parentSeq);

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = GetByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  CompileSnesSeqEventType eventType = static_cast<CompileSnesSeqEventType>(0);
  std::map<uint8_t, CompileSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc = describeUnknownEvent(statusByte);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = GetByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      uint8_t arg4 = GetByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3, arg4);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN5: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      uint8_t arg4 = GetByte(curOffset++);
      uint8_t arg5 = GetByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3, arg4, arg5);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_GOTO: {
      uint16_t dest = GetShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      uint32_t length = curOffset - beginOffset;

      curOffset = dest;
      if (!IsOffsetUsed(dest)) {
        AddGenericEvent(beginOffset, length, "Jump", desc, CLR_LOOPFOREVER);
      }
      else {
        bContinue = AddLoopForever(beginOffset, length, "Jump");
      }
      break;
    }

    case EVENT_LOOP_END: {
      uint8_t repeatNest = GetByte(curOffset++);
      uint16_t dest = GetShort(curOffset);
      curOffset += 2;

      desc = fmt::format("Nest Level: {:d}  Destination: ${:04X}", repeatNest, dest);
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Repeat End", desc, CLR_LOOP, ICON_ENDREP);

      repeatCount[repeatNest]--;
      if (repeatCount[repeatNest] != 0) {
        curOffset = dest;
      }
      break;
    }

    case EVENT_END: {
      AddEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;
    }

    case EVENT_VIBRATO: {
      uint8_t envelopeIndex = GetByte(curOffset++);
      desc = fmt::format("Envelope Index: {:d}", envelopeIndex);
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato",
                      desc,
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PORTAMENTO_TIME: {
      uint8_t rate = GetByte(curOffset++);
      desc = fmt::format("Rate: {:d}", rate);
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Portamento Time",
                      desc,
                      CLR_PORTAMENTOTIME,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VOLUME: {
      uint8_t newVolume = GetByte(curOffset++);
      spcVolume = newVolume;
      uint8_t midiVolume = Convert7bitPercentVolValToStdMidiVal(spcVolume / 2);
      AddVol(beginOffset, curOffset - beginOffset, midiVolume);
      break;
    }

    case EVENT_VOLUME_ENVELOPE: {
      uint8_t envelopeIndex = GetByte(curOffset++);
      desc = fmt::format("Envelope Index: {:d}", envelopeIndex);
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Volume Envelope",
                      desc,
                      CLR_VOLUME,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TRANSPOSE: {
      int8_t delta = static_cast<int8_t>(GetByte(curOffset++));
      spcTranspose += delta;
      AddTranspose(beginOffset, curOffset - beginOffset, spcTranspose, "Transpose (Relative)");
      break;
    }

    case EVENT_VOLUME_REL: {
      int8_t delta = static_cast<int8_t>(GetByte(curOffset++));
      spcVolume += delta;
      uint8_t midiVolume = Convert7bitPercentVolValToStdMidiVal(spcVolume / 2);
      AddVol(beginOffset, curOffset - beginOffset, midiVolume, "Volume (Relative)");
      break;
    }

    case EVENT_NOTE: {
      bool rest = (statusByte == 0x00);

      uint8_t duration;
      bool hasDuration = ReadDurationBytes(curOffset, duration);
      if (hasDuration) {
        spcNoteDuration = duration;
      }

      if (rest) {
        AddRest(beginOffset, curOffset - beginOffset, spcNoteDuration);
      }
      else {
        uint8_t noteNumber = statusByte - 1;
        AddNoteByDur(beginOffset, curOffset - beginOffset, noteNumber, 100, spcNoteDuration,
                     hasDuration ? "Note with Duration" : "Note");
        AddTime(spcNoteDuration);
      }
      break;
    }

    case EVENT_LOOP_COUNT: {
      uint8_t repeatNest = GetByte(curOffset++);
      uint8_t times = GetByte(curOffset++);
      int actualTimes = (times == 0) ? 256 : times;

      desc = fmt::format("Nest Level: {:d}  Times: {:d}", repeatNest, actualTimes);
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop Count", desc, CLR_LOOP, ICON_STARTREP);

      repeatCount[repeatNest] = times;
      break;
    }

    case EVENT_FLAGS: {
      uint8_t flags = GetByte(curOffset++);
      spcFlags = flags;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Flags",
                      desc,
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = GetByte(curOffset++);
      spcTempo = newTempo;
      AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
      break;
    }

    case EVENT_TUNING: {
      int16_t newTuning;
      if (parentSeq->version == COMPILESNES_ALESTE || parentSeq->version == COMPILESNES_JAKICRUSH) {
        newTuning = static_cast<int8_t>(GetByte(curOffset++));
      }
      else {
        newTuning = static_cast<int16_t>(GetShort(curOffset));
        curOffset += 2;
      }

      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tuning",
                      fmt::format("Pitch Register Delta: {:d}", newTuning),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_CALL: {
      uint16_t dest = GetShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);

      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pattern Play",
                      desc,
                      CLR_LOOP,
                      ICON_STARTREP);

      subReturnAddress = curOffset;
      curOffset = dest;
      break;
    }

    case EVENT_RET: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "End Pattern",
                      desc,
                      CLR_TRACKEND,
                      ICON_ENDREP);
      curOffset = subReturnAddress;
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t newProg = GetByte(curOffset++);
      AddProgramChange(beginOffset, curOffset - beginOffset, newProg, true);
      break;
    }

    case EVENT_ADSR: {
      uint8_t envelopeIndex = GetByte(curOffset++);
      desc = fmt::format("Envelope Index: {:d}", envelopeIndex);
      AddGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, CLR_VOLUME, ICON_CONTROL);
      break;
    }

    case EVENT_PORTAMENTO_ON: {
      spcFlags |= COMPILESNES_FLAGS_PORTAMENTO;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Portamento On",
                      desc,
                      CLR_PORTAMENTO,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PORTAMENTO_OFF: {
      spcFlags &= ~COMPILESNES_FLAGS_PORTAMENTO;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Portamento Off",
                      desc,
                      CLR_PORTAMENTO,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PANPOT_ENVELOPE: {
      uint8_t envelopeIndex = GetByte(curOffset++);
      desc = fmt::format("Envelope Index: {:d}", envelopeIndex);
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Panpot Envelope",
                      desc,
                      CLR_PAN,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PAN: {
      int8_t newPan = GetByte(curOffset++);
      spcPan = newPan;

      double volumeScale;
      uint8_t midiPan = Convert7bitLinearPercentPanValToStdMidiVal(static_cast<uint8_t>(newPan + 0x80) / 2, &volumeScale);
      AddExpressionNoItem(ConvertPercentAmpToStdMidiVal(volumeScale));
      AddPan(beginOffset, curOffset - beginOffset, midiPan);
      break;
    }

    case EVENT_LOOP_BREAK: {
      uint8_t repeatNest = GetByte(curOffset++);
      uint16_t dest = GetShort(curOffset);
      curOffset += 2;

      desc = fmt::format("Nest Level: {:d}  Destination: ${:04X}", repeatNest, dest);
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Break", desc, CLR_LOOP, ICON_ENDREP);

      repeatCount[repeatNest]--;
      if (repeatCount[repeatNest] == 0) {
        curOffset = dest;
      }
      break;
    }

    case EVENT_DURATION_DIRECT: {
      uint8_t duration;
      if (!ReadDurationBytes(curOffset, duration)) {
        // address out of range
        return false;
      }
      spcNoteDuration = duration;

      desc = fmt::format("Duration: {:d}", duration);
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Duration (Direct)",
                      desc,
                      CLR_DURNOTE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_DURATION: {
      uint8_t duration;
      if (!ReadDurationBytes(curOffset, duration)) {
        // address out of range
        return false;
      }
      spcNoteDuration = duration;

      desc = fmt::format("Duration: {:d}", duration);
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Duration", desc, CLR_DURNOTE, ICON_CONTROL);
      break;
    }

    case EVENT_PERCUSSION_NOTE: {
      uint8_t duration;
      bool hasDuration = ReadDurationBytes(curOffset, duration);
      if (hasDuration) {
        spcNoteDuration = duration;
      }

      uint8_t percNoteNumber = statusByte - parentSeq->STATUS_PERCUSSION_NOTE_MIN;
      AddPercNoteByDur(beginOffset, curOffset - beginOffset, percNoteNumber, 100, spcNoteDuration,
                       hasDuration ? "Percussion Note with Duration" : "Percussion Note");
      AddTime(spcNoteDuration);
      break;
    }

    default: {
      auto descr = logEvent(statusByte);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }

  //ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}

bool CompileSnesTrack::ReadDurationBytes(uint32_t& offset, uint8_t& duration) const {
  CompileSnesSeq *parentSeq = static_cast<CompileSnesSeq*>(this->parentSeq);

  uint32_t curOffset = offset;
  bool durationDispatched = false;
  while (curOffset < 0x10000) {
    uint8_t statusByte = GetByte(curOffset++);

    if (statusByte == parentSeq->STATUS_DURATION_DIRECT) {
      if (curOffset >= 0x10000) {
        break;
      }

      duration = GetByte(curOffset++);
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
