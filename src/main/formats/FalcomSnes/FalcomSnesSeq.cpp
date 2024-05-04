#include "FalcomSnesSeq.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(FalcomSnes);

//  *************
//  FalcomSnesSeq
//  *************
#define MAX_TRACKS  8
#define SEQ_PPQN    48
#define SEQ_KEYOFS  25

const uint8_t FalcomSnesSeq::VOLUME_TABLE[129] = {
  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,
  63, 62, 61, 60, 59, 58, 57, 56,
  55, 54, 53, 52, 51, 50, 49, 48,
  47, 46, 45, 44, 43, 42, 41, 40,
  39, 38, 37, 36, 35, 34, 33, 32,
  31, 30, 29, 28, 27, 26, 25, 24,
  23, 22, 21, 20, 19, 18, 17, 16,
  15, 14, 13, 12, 11, 10,  9,  8,
   7,  6,  5,  4,  3,  2,  1,  0,
   0
};

FalcomSnesSeq::FalcomSnesSeq(RawFile *file,
                                     FalcomSnesVersion ver,
                                     uint32_t seqdataOffset,
                                     std::string newName)
    : VGMSeq(FalcomSnesFormat::name, file, seqdataOffset, 0, newName), version(ver) {
  bLoadTickByTick = true;
  bAllowDiscontinuousTrackData = true;
  bUseLinearAmplitudeScale = true;

  UseReverb();
  AlwaysWriteInitialReverb(0);

  LoadEventMap();
}

FalcomSnesSeq::~FalcomSnesSeq(void) {
}

void FalcomSnesSeq::ResetVars(void) {
  VGMSeq::ResetVars();

  repeatCountMap.clear();
}

bool FalcomSnesSeq::GetHeaderInfo(void) {
  uint32_t curOffset;

  SetPPQN(SEQ_PPQN);

  VGMHeader *header = AddHeader(dwOffset, 0);
  if (dwOffset + 0x20 > 0x10000) {
    return false;
  }

  curOffset = dwOffset;
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t ofsTrackStart = GetShort(curOffset);
    if (ofsTrackStart != 0) {
      std::stringstream trackName;
      trackName << "Track Pointer " << (trackIndex + 1);
      header->AddSimpleItem(curOffset, 2, trackName.str());
    }
    else {
      header->AddSimpleItem(curOffset, 2, "NULL");
    }
    curOffset += 2;
  }

  header->AddSimpleItem(dwOffset + 0x18, 7, "Duration Table");
  for (uint8_t offset = 0; offset < 7; offset++) {
    NoteDurTable.push_back(GetByte(dwOffset + 0x18 + offset));
  }

  return true;
}

bool FalcomSnesSeq::GetTrackPointers(void) {
  uint32_t curOffset = dwOffset;
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t ofsTrackStart = GetShort(curOffset);
    curOffset += 2;
    if (ofsTrackStart != 0) {
      uint16_t addrTrackStart = dwOffset + ofsTrackStart;
      FalcomSnesTrack *track = new FalcomSnesTrack(this, addrTrackStart);
      aTracks.push_back(track);
    }
  }
  return true;
}

void FalcomSnesSeq::LoadEventMap() {
  int statusByte;

  for (statusByte = 0x00; statusByte <= 0xcf; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  for (statusByte = 0xd0; statusByte <= 0xd6; statusByte++) {
    EventMap[statusByte] = EVENT_OCTAVE;
  }

  EventMap[0xd7] = EVENT_TEMPO;
  EventMap[0xd8] = EVENT_PROGCHANGE;
  EventMap[0xd9] = EVENT_VIBRATO;
  EventMap[0xda] = EVENT_VIBRATO_ON_OFF;
  EventMap[0xdb] = EVENT_NOP3;
  EventMap[0xdc] = EVENT_NOP1;
  EventMap[0xdd] = EVENT_QUANTIZE;
  EventMap[0xde] = EVENT_VOLUME;
  EventMap[0xdf] = EVENT_VOLUME_DEC;
  EventMap[0xe0] = EVENT_VOLUME_DEC;
  EventMap[0xe1] = EVENT_VOLUME_DEC;
  EventMap[0xe2] = EVENT_VOLUME_DEC;
  EventMap[0xe3] = EVENT_VOLUME_INC;
  EventMap[0xe4] = EVENT_VOLUME_INC;
  EventMap[0xe5] = EVENT_VOLUME_INC;
  EventMap[0xe6] = EVENT_VOLUME_INC;
  EventMap[0xe7] = EVENT_PAN;
  EventMap[0xe8] = EVENT_PAN_DEC;
  EventMap[0xe9] = EVENT_PAN_INC;
  EventMap[0xea] = EVENT_PAN_LFO;
  EventMap[0xeb] = EVENT_PAN_LFO_ON_OFF;
  EventMap[0xec] = EVENT_TUNING;
  EventMap[0xed] = EVENT_LOOP_START;
  EventMap[0xee] = EVENT_LOOP_BREAK;
  EventMap[0xef] = EVENT_LOOP_END;
  EventMap[0xf0] = EVENT_PITCH_ENVELOPE;
  EventMap[0xf1] = EVENT_PITCH_ENVELOPE_ON_OFF;
  EventMap[0xf2] = EVENT_ADSR;
  EventMap[0xf3] = EVENT_GAIN;
  EventMap[0xf4] = EVENT_NOISE_FREQ;
  EventMap[0xf5] = EVENT_PITCHMOD;
  EventMap[0xf6] = EVENT_ECHO;
  EventMap[0xf7] = EVENT_ECHO_PARAM;
  EventMap[0xf8] = EVENT_ECHO_VOLUME_ON_OFF;
  EventMap[0xf9] = EVENT_ECHO_VOLUME;
  EventMap[0xfa] = EVENT_ECHO_FIR_OVERWRITE;
  EventMap[0xfb] = EVENT_NOP1;
  EventMap[0xfc] = EVENT_GOTO;
}

double FalcomSnesSeq::GetTempoInBPM(uint8_t tempo) {
  if (tempo != 0) {
    return 60000000.0 / (SEQ_PPQN * (125 * 0x25)) * (tempo / 256.0);
  }
  else {
    // since tempo 0 cannot be expressed, this function returns a very small value.
    return 1.0;
  }
}

//  ***************
//  FalcomSnesTrack
//  ***************

FalcomSnesTrack::FalcomSnesTrack(FalcomSnesSeq *parentFile, long offset, long length)
    : SeqTrack(parentFile, offset, length) {
  ResetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void FalcomSnesTrack::ResetVars(void) {
  SeqTrack::ResetVars();

  cKeyCorrection = SEQ_KEYOFS;

  vel = 100;
  octave = 0;
  prevNoteKey = -1;
  prevNoteSlurred = false;
  spcNoteQuantize = 0;
  spcInstr = 0;
  spcADSR = 0;
  spcVolume = 0;
  spcPan = 0x40;
}

int8_t FalcomSnesTrack::CalcPanValue(uint8_t pan, double &volumeScale) {
  pan &= 0x7f; // value must not be greater than 127

  uint8_t volL = FalcomSnesSeq::VOLUME_TABLE[pan];
  uint8_t volR = FalcomSnesSeq::VOLUME_TABLE[(0x7f - pan + 1) & 0x7f];
  double volumeLeft = volL / 64.0;
  double volumeRight = volR / 64.0;

  int8_t midiPan = ConvertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &volumeScale);
  volumeScale /= sqrt(2);

  return midiPan;
}

bool FalcomSnesTrack::ReadEvent(void) {
  FalcomSnesSeq *parentSeq = (FalcomSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = GetByte(curOffset++);
  bool bContinue = true;

  std::stringstream desc;

  FalcomSnesSeqEventType eventType = (FalcomSnesSeqEventType) 0;
  std::map<uint8_t, FalcomSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

    case EVENT_NOP1: {
      uint8_t arg1 = GetByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "NOP.1", desc.str(), CLR_MISC, ICON_BINARY);
      break;
    }

    case EVENT_NOP3: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3;
      AddUnknown(beginOffset, curOffset - beginOffset, "NOP.3", desc.str());
      break;
    }

    case EVENT_NOTE: {
      uint8_t lenIndex = statusByte & 7;
      bool noKeyoff = (statusByte & 8) != 0; // i.e. slur/tie
      uint8_t keyIndex = statusByte >> 4;

      uint8_t len;
      if (lenIndex != 0) {
        len = parentSeq->NoteDurTable[lenIndex - 1];
      }
      else {
        len = GetByte(curOffset++);
      }

      uint8_t dur;
      if (noKeyoff || spcNoteQuantize == 0) {
        // slur/tie = full-length
        dur = len;
      }
      else {
        uint8_t durDelta = (len * spcNoteQuantize) >> 8;
        dur = len - durDelta;
      }

      bool rest = (keyIndex == 12);
      if (rest) {
        AddRest(beginOffset, curOffset - beginOffset, len);
        prevNoteSlurred = false;
      }
      else {
        if (parentSeq->instrADSRHints.find(spcInstr) == parentSeq->instrADSRHints.end()) {
          parentSeq->instrADSRHints[spcInstr] = spcADSR;
        }

        int8_t key = (octave * 12) + (keyIndex - 1);
        if (prevNoteSlurred && key == prevNoteKey) {
          // tie
          MakePrevDurNoteEnd(GetTime() + dur);
          AddGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc.str().c_str(), CLR_TIE, ICON_NOTE);
        }
        else {
          // note
          AddNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur);
          prevNoteKey = key;
        }
        prevNoteSlurred = noKeyoff;
        AddTime(len);
      }

      break;
    }

    case EVENT_OCTAVE: {
      uint8_t newOctave = statusByte - 0xd0;
      AddSetOctave(beginOffset, curOffset - beginOffset, newOctave);
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = GetByte(curOffset++); // default value: 120
      AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t instrNum = GetByte(curOffset++);
      spcInstr = instrNum;
	  spcADSR = 0; // use default
      AddProgramChange(beginOffset, curOffset - beginOffset, instrNum);
      break;
    }

    case EVENT_VIBRATO: {
      uint8_t vibratoDelay = GetByte(curOffset++);
      uint8_t vibratoDepth = GetByte(curOffset++);
      uint8_t vibratoRate = GetByte(curOffset++);

      desc << "Delay: " << (int) vibratoDelay
           << "  Depth: " << (int) vibratoDepth
           << "  Rate: " << (int) vibratoRate;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_ON_OFF: {
      bool vibratoOn = GetByte(curOffset++) != 0;

      desc << "Vibrato: " << (vibratoOn ? "On" : "Off");
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato On/Off", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
      break;
    }

    case EVENT_QUANTIZE: {
      uint8_t newQuantize = GetByte(curOffset++);
      desc << "Duration Rate: 1.0 - " << newQuantize << "/256";
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate", desc.str().c_str(), CLR_DURNOTE);
      spcNoteQuantize = newQuantize;
      break;
    }

    case EVENT_VOLUME: {
      uint8_t newVolume = GetByte(curOffset++);
      spcVolume = newVolume;

      // actual engine does not limit value here,
      // but the volume value must not be greater than 127
      uint8_t midiVolume = std::min(newVolume, (uint8_t)127);
      AddVol(beginOffset, curOffset - beginOffset, midiVolume);
      break;
    }

    case EVENT_VOLUME_DEC: {
      uint8_t amounts[] = { 8, 1, 2, 4 };
      uint8_t amount = amounts[statusByte - 0xdf];

      desc << "Decrease Volume by : " << amount;
      AddGenericEvent(beginOffset, curOffset - beginOffset, desc.str(), "", CLR_VOLUME, ICON_CONTROL);

      // add MIDI events only if updated
      uint8_t newVolume = (uint8_t)std::max((int8_t)spcVolume - amount, 0);
      if (newVolume != spcVolume) {
        spcVolume = newVolume;
        AddVolNoItem(spcVolume);
      }
      break;
    }

    case EVENT_VOLUME_INC: {
      uint8_t amounts[] = { 8, 1, 2, 4 };
      uint8_t amount = amounts[statusByte - 0xe3];

      desc << "Increase Volume by : " << amount;
      AddGenericEvent(beginOffset, curOffset - beginOffset, desc.str(), "", CLR_VOLUME, ICON_CONTROL);

      // add MIDI events only if updated
      uint8_t newVolume = (uint8_t)std::min(spcVolume + amount, 0x7f);
      if (newVolume != spcVolume) {
        spcVolume = newVolume;
        AddVolNoItem(spcVolume);
      }
      break;
    }

    case EVENT_PAN: {
      uint8_t newPan = GetByte(curOffset++);
      spcPan = newPan;

      double volumeScale;
      int8_t midiPan = CalcPanValue(newPan, volumeScale);
      AddPan(beginOffset, curOffset - beginOffset, midiPan);
      AddExpressionNoItem(ConvertPercentAmpToStdMidiVal(volumeScale));
      break;
    }

    case EVENT_PAN_DEC: {
      uint8_t amount = 8;

      desc << "Decrease Pan by : " << amount;
      AddGenericEvent(beginOffset, curOffset - beginOffset, desc.str(), "", CLR_PAN, ICON_CONTROL);

      // add MIDI events only if updated
      uint8_t newPan = (uint8_t)std::max((int8_t)spcPan - amount, 0);
      if (newPan != spcPan) {
        spcPan = newPan;

        double volumeScale;
        int8_t midiPan = CalcPanValue(newPan, volumeScale);
        AddPanNoItem(midiPan);
        AddExpressionNoItem(ConvertPercentAmpToStdMidiVal(volumeScale));
      }
      break;
    }

    case EVENT_PAN_INC: {
      uint8_t amount = 8;

      desc << "Increase Pan by : " << amount;
      AddGenericEvent(beginOffset, curOffset - beginOffset, desc.str(), "", CLR_PAN, ICON_CONTROL);

      // add MIDI events only if updated
      uint8_t newPan = (uint8_t)std::min(spcPan + amount, 0x7f);
      if (newPan != spcPan) {
        spcPan = newPan;

        double volumeScale;
        int8_t midiPan = CalcPanValue(newPan, volumeScale);
        AddPanNoItem(midiPan);
        AddExpressionNoItem(ConvertPercentAmpToStdMidiVal(volumeScale));
      }
      break;
    }

    case EVENT_PAN_LFO: {
      uint8_t lfoDelay = GetByte(curOffset++);
      uint8_t lfoDepth = GetByte(curOffset++);
      uint8_t lfoRate = GetByte(curOffset++);

      desc << "Delay: " << (int) lfoDelay
           << "  Depth: " << (int) lfoDepth
           << "  Rate: " << (int) lfoRate;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO_ON_OFF: {
      bool lfoOn = GetByte(curOffset++) != 0;

      desc << "Pan LFO: " << (lfoOn ? "On" : "Off");
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO On/Off", desc.str().c_str(), CLR_MODULATION, ICON_CONTROL);
      break;
    }

    case EVENT_TUNING: {
      int8_t newTuning = GetByte(curOffset++);
      desc << "Hearz: " << (newTuning >= 0 ? "+" : "") << newTuning << " Hz";
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Fine Tuning", desc.str(), CLR_TRANSPOSE, ICON_CONTROL);

      // TODO: more accurate fine tuning
      AddFineTuningNoItem(newTuning / 128.0 * 64.0);

      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = GetByte(curOffset++);
      desc << "Loop Count: " << (int) count;
      uint16_t repeatCountAddr = curOffset++;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

      // save the repeat count
      parentSeq->repeatCountMap[repeatCountAddr] = count;
      break;
    }

    case EVENT_LOOP_BREAK: {
      int16_t destOffset = GetShort(curOffset);
      curOffset += 2;
      uint16_t dest = curOffset + destOffset;
      int16_t repeatCountOffset = GetShort(dest - 2);
      uint16_t repeatCountAddr = dest + repeatCountOffset;
      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest <<
              "Loop Count Address: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) repeatCountAddr;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

      if (parentSeq->repeatCountMap.count(repeatCountAddr) == 0) {
        bContinue = false;
        break;
      }

      if (parentSeq->repeatCountMap[repeatCountAddr] == 1) {
        curOffset = dest;
      }

      break;
    }

    case EVENT_LOOP_END: {
      int16_t destOffset = GetShort(curOffset);
      curOffset += 2;
      uint16_t repeatCountAddr = curOffset + destOffset;
      uint16_t dest = repeatCountAddr + 1;
      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

      if (parentSeq->repeatCountMap.count(repeatCountAddr) == 0) {
        bContinue = false;
        break;
      }

      parentSeq->repeatCountMap[repeatCountAddr]--;
      if (parentSeq->repeatCountMap[repeatCountAddr] != 0) {
        // repeat again
        curOffset = dest;
      }

      break;
    }

    case EVENT_PITCH_ENVELOPE: {
      uint8_t pitchEnvelopeDelay = GetByte(curOffset++);
      uint8_t pitchEnvelopeDepth = GetByte(curOffset++);
      uint8_t pitchEnvelopeRate = GetByte(curOffset++);

      desc << "Delay: " << (int) pitchEnvelopeDelay
           << "  Depth: " << (int) pitchEnvelopeDepth
           << "  Rate: " << (int) pitchEnvelopeRate;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
      break;
    }

    case EVENT_PITCH_ENVELOPE_ON_OFF: {
      bool pitchEnvelopeOn = GetByte(curOffset++) != 0;

      desc << "Pitch Envelope: " << (pitchEnvelopeOn ? "On" : "Off");
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope On/Off", desc.str().c_str(), CLR_PITCHBEND, ICON_CONTROL);
      break;
    }

    case EVENT_ADSR: {
      uint8_t adsr1 = GetByte(curOffset++);
      uint8_t adsr2 = GetByte(curOffset++);
	  spcADSR = adsr1 | (adsr2 << 8);
      desc << "ADSR(1): $" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << adsr1 <<
          "  ADSR(2): $" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << adsr2;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_GAIN: {
      // Note: This command doesn't work properly on Ys V
      uint8_t newGAIN = GetByte(curOffset++);

      desc << "GAIN: $" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) newGAIN << ")";
      AddGenericEvent(beginOffset, curOffset - beginOffset, "GAIN", desc.str().c_str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_NOISE_FREQ: {
      uint8_t newNCK = GetByte(curOffset++) & 0x1f;
      desc << "Noise Frequency (NCK): " << (int) newNCK;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Noise Frequency", desc.str().c_str(), CLR_CHANGESTATE, ICON_CONTROL);
      break;
    }

    case EVENT_PITCHMOD: {
      bool echoOn = (GetByte(curOffset++) != 0);
      desc << "Pitch Modulation: " << (echoOn ? "On" : "Off");
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Echo", desc.str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_ECHO: {
      bool echoOn = (GetByte(curOffset++) != 0);
      desc << "Echo Write: " << (echoOn ? "On" : "Off");
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Echo", desc.str(), CLR_REVERB, ICON_CONTROL);
      AddReverbNoItem(echoOn ? 40 : 0);
      break;
    }

    case EVENT_ECHO_PARAM: {
      uint8_t spcEDL = GetByte(curOffset++);
      uint8_t spcEFB = GetByte(curOffset++);
      uint8_t spcFIR = GetByte(curOffset++);

      desc << "Delay: " << (int) spcEDL << "  Feedback: " << (int) spcEFB << "  FIR: " << (int) spcFIR;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Echo Param", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);

      // enable echo for the channel
      AddReverbNoItem(40);
      break;
    }

    case EVENT_ECHO_VOLUME_ON_OFF: {
      bool echoVolumeOn = GetByte(curOffset++) != 0;

      desc << "Echo Volume: " << (echoVolumeOn ? "On" : "Off");
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume On/Off", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_VOLUME: {
      int8_t echoVolumeLeft = GetByte(curOffset++);
      int8_t echoVolumeRight = GetByte(curOffset++);
      desc << "Echo Volume Left: " << echoVolumeLeft << "  Echo Volume Right: " << echoVolumeRight;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume", desc.str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_FIR_OVERWRITE: {
      uint8_t presetNo = GetByte(curOffset++);
      curOffset += 8; // FIR C0-C7
	  desc << "Preset: #" << presetNo;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Echo FIR Overwrite", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_GOTO: {
      int16_t destOffset = GetShort(curOffset);
      curOffset += 2;

      if (destOffset == 0) {
        AddEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
      }
      else {
        uint16_t dest = curOffset + destOffset;
        desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
        uint32_t length = curOffset - beginOffset;

        curOffset = dest;
        if (!IsOffsetUsed(dest)) {
          AddGenericEvent(beginOffset, length, "Jump", desc.str().c_str(), CLR_LOOPFOREVER);
        }
        else {
          bContinue = AddLoopForever(beginOffset, length, "Jump");
        }
      }
      break;
    }

    default:
      auto description = logEvent(statusByte);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", description);
      bContinue = false;
      break;
  }

  //std::ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}
