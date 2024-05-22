#include "PandoraBoxSnesSeq.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(PandoraBoxSnes);

static constexpr int kMaxTracks = 8;
static constexpr uint8_t kNoteVelocity = 100;

//  *****************
//  PandoraBoxSnesSeq
//  *****************

const uint8_t PandoraBoxSnesSeq::VOLUME_TABLE[16] = {
    0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c,
    0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c,
};

PandoraBoxSnesSeq::PandoraBoxSnesSeq(RawFile *file,
                                     PandoraBoxSnesVersion ver,
                                     uint32_t seqdataOffset,
                                     std::string newName)
    : VGMSeq(PandoraBoxSnesFormat::name, file, seqdataOffset, 0, newName), version(ver) {
  bLoadTickByTick = true;
  bAllowDiscontinuousTrackData = true;
  bUseLinearAmplitudeScale = true;

  UseReverb();
  AlwaysWriteInitialReverb(0);

  LoadEventMap();
}

PandoraBoxSnesSeq::~PandoraBoxSnesSeq() {
}

void PandoraBoxSnesSeq::ResetVars() {
  VGMSeq::ResetVars();

  AlwaysWriteInitialTempo(GetByte(dwOffset + 6));
}

bool PandoraBoxSnesSeq::GetHeaderInfo() {
  uint32_t curOffset;

  VGMHeader *header = AddHeader(dwOffset, 0);
  if (dwOffset + 0x20 > 0x10000) {
    return false;
  }

  AddSimpleItem(dwOffset + 6, 1, "Tempo");
  AddSimpleItem(dwOffset + 7, 1, "Timebase");

  uint8_t timebase = GetByte(dwOffset + 7);
  assert((timebase % 4) == 0);
  SetPPQN(timebase / 4);

  curOffset = dwOffset + 0x10;
  for (uint8_t trackIndex = 0; trackIndex < kMaxTracks; trackIndex++) {
    uint16_t ofsTrackStart = GetShort(curOffset);
    if (ofsTrackStart != 0xffff) {
      std::stringstream trackName;
      trackName << "Track Pointer " << (trackIndex + 1);
      header->AddSimpleItem(curOffset, 2, trackName.str());
    }
    else {
      header->AddSimpleItem(curOffset, 2, "NULL");
    }
    curOffset += 2;
  }

  return true;
}

bool PandoraBoxSnesSeq::GetTrackPointers() {
  uint32_t curOffset = dwOffset + 0x10;
  for (uint8_t trackIndex = 0; trackIndex < kMaxTracks; trackIndex++) {
    uint16_t ofsTrackStart = GetShort(curOffset);
    curOffset += 2;
    if (ofsTrackStart != 0xffff) {
      uint16_t addrTrackStart = dwOffset + ofsTrackStart;
      PandoraBoxSnesTrack *track = new PandoraBoxSnesTrack(this, addrTrackStart);
      aTracks.push_back(track);
    }
  }
  return true;
}

void PandoraBoxSnesSeq::LoadEventMap() {
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

PandoraBoxSnesTrack::PandoraBoxSnesTrack(PandoraBoxSnesSeq *parentFile, long offset, long length)
    : SeqTrack(parentFile, offset, length) {
  ResetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void PandoraBoxSnesTrack::ResetVars() {
  SeqTrack::ResetVars();

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

bool PandoraBoxSnesTrack::ReadEvent() {
  PandoraBoxSnesSeq *parentSeq = (PandoraBoxSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = GetByte(curOffset++);
  bool bContinue = true;

  std::stringstream desc;

  PandoraBoxSnesSeqEventType eventType = (PandoraBoxSnesSeqEventType) 0;
  std::map<uint8_t, PandoraBoxSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = GetByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      uint8_t arg4 = GetByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3
          << "  Arg4: " << (int) arg4;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;
    }

    case EVENT_NOP: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc.str(), CLR_MISC, ICON_BINARY);
      break;
    }

    case EVENT_NOTE: {
      uint8_t keyIndex = statusByte & 15;
      bool noKeyoff = (statusByte & 0x10) != 0; // i.e. slur/tie
      bool hasLength = (statusByte & 0x20) == 0;

      uint8_t len;
      if (hasLength) {
        len = GetByte(curOffset++);
        spcNoteLength = len;
      }
      else {
        len = spcNoteLength;
      }

      uint8_t dur;
      if (noKeyoff || spcNoteQuantize == 0) {
        dur = len;
      }
      else {
        dur = len * spcNoteQuantize / 8;
      }

      bool rest = (keyIndex == 0);
      if (rest) {
        AddRest(beginOffset, curOffset - beginOffset, len);
        prevNoteSlurred = false;
      }
      else {
        // a note, add hints for instrument
        if (parentSeq->instrADSRHints.find(spcInstr) == parentSeq->instrADSRHints.end()) {
          parentSeq->instrADSRHints[spcInstr] = spcADSR;
        }

        int8_t key = (octave * 12) + (keyIndex - 1);
        if (prevNoteSlurred && key == prevNoteKey) {
          // tie
          MakePrevDurNoteEnd(GetTime() + dur);
          AddGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc.str(), CLR_TIE, ICON_NOTE);
        }
        else {
          // note
          AddNoteByDur(beginOffset, curOffset - beginOffset, key, kNoteVelocity, dur);
          prevNoteKey = key;
        }
        prevNoteSlurred = noKeyoff;
        AddTime(len);
      }

      break;
    }

    case EVENT_OCTAVE: {
      octave = (statusByte - 0x40);
      desc << "Octave: " << octave;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Octave", desc.str(), CLR_CHANGESTATE);
      break;
    }

    case EVENT_QUANTIZE: {
      spcNoteQuantize = (statusByte - 0x48);
      desc << "Length: " << spcNoteQuantize << "/8";
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate", desc.str(), CLR_DURNOTE);
      break;
    }

    case EVENT_VOLUME_FROM_TABLE: {
      uint8_t newVolumeIndex = (statusByte - 0x50);

      uint8_t newVolume = GetVolume(newVolumeIndex);
      desc << "Volume Index: " << spcVolumeIndex << " (" << newVolume << ")";

      AddVol(beginOffset, curOffset - beginOffset, newVolume, "Volume From Table");
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t instrNum = (statusByte - 0x60);
      spcInstr = instrNum;
      AddProgramChange(beginOffset, curOffset - beginOffset, instrNum);
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = GetByte(curOffset++);
      AddTempoBPM(beginOffset, curOffset - beginOffset, newTempo);
      break;
    }

    case EVENT_TUNING: {
      int8_t newTuning = GetByte(curOffset++);
      desc << "Hearz: " << (newTuning >= 0 ? "+" : "") << newTuning << " Hz";
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Fine Tuning", desc.str(), CLR_TRANSPOSE, ICON_CONTROL);
      break;
    }

    case EVENT_TRANSPOSE: {
      int8_t newTranspose = GetByte(curOffset++);
      AddTranspose(beginOffset, curOffset - beginOffset, newTranspose);
      break;
    }

    case EVENT_PAN: {
      uint8_t newPan = GetByte(curOffset++);

      newPan = std::min(newPan, (uint8_t) 128);
      double linearPan = newPan / 128.0;
      double volumeScale;
      int8_t midiPan = ConvertLinearPercentPanValToStdMidiVal(linearPan, &volumeScale);

      AddPan(beginOffset, curOffset - beginOffset, midiPan);
      AddExpressionNoItem(std::round(127.0 * volumeScale));
      break;
    }

    case EVENT_INC_OCTAVE: {
      AddIncrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_DEC_OCTAVE: {
      AddDecrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_INC_VOLUME: {
      if (spcVolumeIndex < 15) {
        spcVolumeIndex++;
      }

      uint8_t newVolume = GetVolume(spcVolumeIndex);
      desc << "Volume Index: " << spcVolumeIndex << " (" << newVolume << ")";

      AddVol(beginOffset, curOffset - beginOffset, newVolume, "Increase Volume");
      break;
    }

    case EVENT_DEC_VOLUME: {
      if (spcVolumeIndex > 0) {
        spcVolumeIndex--;
      }

      uint8_t newVolume = GetVolume(spcVolumeIndex);
      desc << "Volume Index: " << spcVolumeIndex << " (" << newVolume << ")";

      AddVol(beginOffset, curOffset - beginOffset, newVolume, "Decrease Volume");
      break;
    }

    case EVENT_VIBRATO_PARAM: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      uint8_t arg4 = GetByte(curOffset++);
      uint8_t arg5 = GetByte(curOffset++);

      desc << "Arg1: " << arg1 <<
          "  Arg2: " << arg2 <<
          "  Arg3: " << arg3 <<
          "  Arg4: " << arg4 <<
          "  Arg5: " << arg5;

      AddGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Param", desc.str(), CLR_MODULATION, ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO: {
      bool vibratoOn = (GetByte(curOffset++) != 0);
      desc << "Vibrato: " << (vibratoOn ? "On" : "Off");
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato On/Off",
                      desc.str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_OFF: {
      AddReverb(beginOffset, curOffset - beginOffset, 0, "Echo Off");
      break;
    }

    case EVENT_ECHO_ON: {
      AddReverb(beginOffset, curOffset - beginOffset, 40, "Echo On");
      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = GetByte(curOffset++);
      desc << "Times: " << (int) count;
      AddGenericEvent(beginOffset, 2, "Loop Start", desc.str(), CLR_LOOP, ICON_STARTREP);

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
        bContinue = AddLoopForever(beginOffset, curOffset - beginOffset, "Loop End");
      }
      else {
        // regular loop
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc.str(), CLR_LOOP, ICON_ENDREP);

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
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", desc.str(), CLR_LOOP, ICON_ENDREP);

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
      uint8_t dspReg = GetByte(curOffset++);
      uint8_t dspValue = GetByte(curOffset++);
      desc << "Register: $" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) dspReg
          << "  Value: $" << (int) dspValue;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Write to DSP", desc.str(), CLR_CHANGESTATE);
      break;
    }

    case EVENT_NOISE_PARAM: {
      uint8_t param = GetByte(curOffset++);

      uint8_t newTarget = param & 3;
      if (newTarget != 0) {
        desc << "Channel Type: " << newTarget;
      }
      else {
        desc << "Channel Type: " << newTarget << " (Keep Current)";
      }

      if ((param & 0x80) == 0) {
        uint8_t newNCK = (param >> 2) & 15;
        desc << "  Noise Frequency (NCK): " << newNCK;
      }
      else {
        desc << "  Noise Frequency (NCK): " << " (Keep Current)";
      }

      AddGenericEvent(beginOffset, curOffset - beginOffset, "Noise Param", desc.str(), CLR_CHANGESTATE, ICON_CONTROL);
      break;
    }

    case EVENT_ADSR: {
      uint8_t arRate = GetByte(curOffset++);
      uint8_t drRate = GetByte(curOffset++);
      uint8_t srRate = GetByte(curOffset++);
      uint8_t slRate = GetByte(curOffset++);
      uint8_t xxRate = GetByte(curOffset++);

      uint8_t ar = (arRate * 0x0f) / 127;
      uint8_t dr = (drRate * 0x07) / 127;
      uint8_t sl = 7 - ((slRate * 0x07) / 127);
      uint8_t sr = (srRate * 0x1f) / 127;
      spcADSR = ((0x80 | (dr << 4) | ar) << 8) | ((sl << 5) | sr);

      desc << "AR: " << arRate << "/127" << " (" << ar << ")" <<
          "  DR: " << drRate << "/127" << " (" << dr << ")" <<
          "  SR: " << srRate << "/127" << " (" << sr << ")" <<
          "  SL: " << slRate << "/127" << " (" << sl << ")" <<
          "  Arg5: " << xxRate << "/127";

      AddGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_END:
      AddEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;

    case EVENT_VOLUME: {
      uint8_t newVolumeIndex = GetByte(curOffset++);
      spcVolumeIndex = newVolumeIndex;

      uint8_t newVolume = GetVolume(newVolumeIndex);
      AddVol(beginOffset, curOffset - beginOffset, newVolume);
      break;
    }

    default: {
      auto descr = logEvent(statusByte);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }

  //std::ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}

uint8_t PandoraBoxSnesTrack::GetVolume(uint8_t volumeIndex) {
  PandoraBoxSnesSeq *parentSeq = (PandoraBoxSnesSeq *) this->parentSeq;

  uint8_t volume;

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
  volume = std::min(volume, (uint8_t) 0x7f);

  return volume;
}
