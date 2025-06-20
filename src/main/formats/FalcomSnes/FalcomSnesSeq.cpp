#include "FalcomSnesSeq.h"
#include "ScaleConversion.h"
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(FalcomSnes);

static constexpr int MAX_TRACKS = 8;
static constexpr uint16_t SEQ_PPQN = 48;
static constexpr int SEQ_KEY_OFFSET = 25;
static constexpr uint8_t NOTE_VELOCITY = 100;

//  *************
//  FalcomSnesSeq
//  *************

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
                                     std::string name)
    : VGMSeq(FalcomSnesFormat::name, file, seqdataOffset, 0, std::move(name)), version(ver) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

FalcomSnesSeq::~FalcomSnesSeq() {
}

void FalcomSnesSeq::resetVars() {
  VGMSeq::resetVars();

  repeatCountMap.clear();
}

bool FalcomSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(dwOffset, 0);
  if (dwOffset + 0x20 > 0x10000) {
    return false;
  }

  uint32_t curOffset = dwOffset;
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t ofsTrackStart = readShort(curOffset);
    if (ofsTrackStart != 0) {
      auto trackName = fmt::format("Track Pointer {}", trackIndex + 1);
      header->addChild(curOffset, 2, trackName);
    }
    else {
      header->addChild(curOffset, 2, "NULL");
    }
    curOffset += 2;
  }

  header->addChild(dwOffset + 0x18, 7, "Duration Table");
  for (uint8_t offset = 0; offset < 7; offset++) {
    NoteDurTable.push_back(readByte(dwOffset + 0x18 + offset));
  }

  return true;
}

bool FalcomSnesSeq::parseTrackPointers() {
  uint32_t curOffset = dwOffset;
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t ofsTrackStart = readShort(curOffset);
    curOffset += 2;
    if (ofsTrackStart != 0) {
      uint16_t addrTrackStart = dwOffset + ofsTrackStart;
      FalcomSnesTrack *track = new FalcomSnesTrack(this, addrTrackStart);
      aTracks.push_back(track);
    }
  }
  return true;
}

void FalcomSnesSeq::loadEventMap() {
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

double FalcomSnesSeq::getTempoInBPM(uint8_t tempo) {
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

FalcomSnesTrack::FalcomSnesTrack(FalcomSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  FalcomSnesTrack::resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void FalcomSnesTrack::resetVars() {
  SeqTrack::resetVars();

  cKeyCorrection = SEQ_KEY_OFFSET;

  octave = 0;
  prevNoteKey = -1;
  prevNoteSlurred = false;
  spcNoteQuantize = 0;
  spcInstr = 0;
  spcADSR = 0;
  spcVolume = 0;
  spcPan = 0x40;
}

int8_t FalcomSnesTrack::calculatePanValue(uint8_t pan, double &volumeScale) {
  pan &= 0x7f; // value must not be greater than 127

  uint8_t volL = FalcomSnesSeq::VOLUME_TABLE[pan];
  uint8_t volR = FalcomSnesSeq::VOLUME_TABLE[(0x7f - pan + 1) & 0x7f];
  double volumeLeft = volL / 64.0;
  double volumeRight = volR / 64.0;

  int8_t midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &volumeScale);
  volumeScale /= sqrt(2);

  return midiPan;
}

bool FalcomSnesTrack::readEvent() {
  FalcomSnesSeq *parentSeq = static_cast<FalcomSnesSeq*>(this->parentSeq);

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  FalcomSnesSeqEventType eventType = static_cast<FalcomSnesSeqEventType>(0);
  std::map<uint8_t, FalcomSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

    case EVENT_NOP1: {
      uint8_t arg1 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1);
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP.1", desc, Type::Nop);
      break;
    }

    case EVENT_NOP3: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "NOP.3", desc);
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
        len = readByte(curOffset++);
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
        addRest(beginOffset, curOffset - beginOffset, len);
        prevNoteSlurred = false;
      }
      else {
        if (!parentSeq->instrADSRHints.contains(spcInstr)) {
          parentSeq->instrADSRHints[spcInstr] = spcADSR;
        }

        int8_t key = (octave * 12) + (keyIndex - 1);
        if (prevNoteSlurred && key == prevNoteKey) {
          // tie
          makePrevDurNoteEnd(getTime() + dur);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc, Type::Tie);
        }
        else {
          // note
          addNoteByDur(beginOffset, curOffset - beginOffset, key, NOTE_VELOCITY, dur);
          prevNoteKey = key;
        }
        prevNoteSlurred = noKeyoff;
        addTime(len);
      }

      break;
    }

    case EVENT_OCTAVE: {
      uint8_t newOctave = statusByte - 0xd0;
      addSetOctave(beginOffset, curOffset - beginOffset, newOctave);
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = readByte(curOffset++); // default value: 120
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM(newTempo));
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t instrNum = readByte(curOffset++);
      spcInstr = instrNum;
	    spcADSR = 0; // use default
      addProgramChange(beginOffset, curOffset - beginOffset, instrNum);
      break;
    }

    case EVENT_VIBRATO: {
      uint8_t vibratoDelay = readByte(curOffset++);
      uint8_t vibratoDepth = readByte(curOffset++);
      uint8_t vibratoRate = readByte(curOffset++);

      desc = fmt::format("Delay: {:d}  Depth: {:d}  Rate: {:d}", vibratoDelay, vibratoDepth, vibratoRate);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO_ON_OFF: {
      bool vibratoOn = readByte(curOffset++) != 0;

      desc = fmt::format("Vibrato: {}", vibratoOn ? "On" : "Off");
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato On/Off", desc, Type::Vibrato);
      break;
    }

    case EVENT_QUANTIZE: {
      uint8_t newQuantize = readByte(curOffset++);

      desc = fmt::format("Duration Rate: 1.0 - {:d}/256", newQuantize);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate", desc, Type::DurationNote);
      spcNoteQuantize = newQuantize;
      break;
    }

    case EVENT_VOLUME: {
      uint8_t newVolume = readByte(curOffset++);
      spcVolume = newVolume;

      // actual engine does not limit value here,
      // but the volume value must not be greater than 127
      uint8_t midiVolume = std::min(newVolume, static_cast<uint8_t>(127));
      addVol(beginOffset, curOffset - beginOffset, midiVolume);
      break;
    }

    case EVENT_VOLUME_DEC: {
      uint8_t amounts[] = { 8, 1, 2, 4 };
      uint8_t amount = amounts[statusByte - 0xdf];

      desc = fmt::format("Decrease Volume by : {:d}", amount);
      addGenericEvent(beginOffset, curOffset - beginOffset, desc, "", Type::Volume);

      // add MIDI events only if updated
      uint8_t newVolume = static_cast<uint8_t>(std::max(static_cast<int8_t>(spcVolume) - amount, 0));
      if (newVolume != spcVolume) {
        spcVolume = newVolume;
        addVolNoItem(spcVolume);
      }
      break;
    }

    case EVENT_VOLUME_INC: {
      uint8_t amounts[] = { 8, 1, 2, 4 };
      uint8_t amount = amounts[statusByte - 0xe3];

      desc = fmt::format("Increase Volume by : {:d}", amount);
      addGenericEvent(beginOffset, curOffset - beginOffset, desc, "", Type::Volume);

      // add MIDI events only if updated
      uint8_t newVolume = static_cast<uint8_t>(std::min(spcVolume + amount, 0x7f));
      if (newVolume != spcVolume) {
        spcVolume = newVolume;
        addVolNoItem(spcVolume);
      }
      break;
    }

    case EVENT_PAN: {
      uint8_t newPan = readByte(curOffset++);
      spcPan = newPan;

      double volumeScale;
      int8_t midiPan = calculatePanValue(newPan, volumeScale);
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));
      break;
    }

    case EVENT_PAN_DEC: {
      uint8_t amount = 8;

      desc = fmt::format("Decrease Pan by : {:d}", amount);
      addGenericEvent(beginOffset, curOffset - beginOffset, desc, "", Type::Pan);

      // add MIDI events only if updated
      uint8_t newPan = static_cast<uint8_t>(std::max(static_cast<int8_t>(spcPan) - amount, 0));
      if (newPan != spcPan) {
        spcPan = newPan;

        double volumeScale;
        int8_t midiPan = calculatePanValue(newPan, volumeScale);
        addPanNoItem(midiPan);
        addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));
      }
      break;
    }

    case EVENT_PAN_INC: {
      uint8_t amount = 8;

      desc = fmt::format("Increase Pan by : {:d}", amount);
      addGenericEvent(beginOffset, curOffset - beginOffset, desc, "", Type::Pan);

      // add MIDI events only if updated
      uint8_t newPan = static_cast<uint8_t>(std::min(spcPan + amount, 0x7f));
      if (newPan != spcPan) {
        spcPan = newPan;

        double volumeScale;
        int8_t midiPan = calculatePanValue(newPan, volumeScale);
        addPanNoItem(midiPan);
        addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));
      }
      break;
    }

    case EVENT_PAN_LFO: {
      uint8_t lfoDelay = readByte(curOffset++);
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);

      desc = fmt::format("Delay: {:d}  Depth: {:d}  Rate: {:d}", lfoDelay, lfoDepth, lfoRate);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO", desc, Type::PanLfo);
      break;
    }

    case EVENT_PAN_LFO_ON_OFF: {
      bool lfoOn = readByte(curOffset++) != 0;

      desc = fmt::format("Pan LFO: {}", lfoOn ? "On" : "Off");
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO On/Off", desc, Type::PanLfo);
      break;
    }

    case EVENT_TUNING: {
      int8_t newTuning = readByte(curOffset++);

      desc = fmt::format("Herz: {}{}Hz", newTuning >= 0 ? "+" : "-", newTuning);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Fine Tuning", desc, Type::FineTune);

      // TODO: more accurate fine tuning
      addFineTuningNoItem(newTuning / 128.0 * 64.0);
      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = readByte(curOffset++);

      desc = fmt::format("Loop Count {:d}", count);
      uint16_t repeatCountAddr = curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", desc, Type::RepeatStart);

      // save the repeat count
      parentSeq->repeatCountMap[repeatCountAddr] = count;
      break;
    }

    case EVENT_LOOP_BREAK: {
      int16_t destOffset = readShort(curOffset);
      curOffset += 2;
      uint16_t dest = curOffset + destOffset;
      int16_t repeatCountOffset = readShort(dest - 2);
      uint16_t repeatCountAddr = dest + repeatCountOffset;
      desc = fmt::format("Destination: ${:04X}  Loop Count Address: ${:04X}", dest, repeatCountAddr);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", desc, Type::LoopBreak);

      if (!parentSeq->repeatCountMap.contains(repeatCountAddr)) {
        bContinue = false;
        break;
      }

      if (parentSeq->repeatCountMap[repeatCountAddr] == 1) {
        curOffset = dest;
      }

      break;
    }

    case EVENT_LOOP_END: {
      int16_t destOffset = readShort(curOffset);
      curOffset += 2;
      uint16_t repeatCountAddr = curOffset + destOffset;
      uint16_t dest = repeatCountAddr + 1;
      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc, Type::RepeatEnd);

      if (!parentSeq->repeatCountMap.contains(repeatCountAddr)) {
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
      uint8_t pitchEnvelopeDelay = readByte(curOffset++);
      uint8_t pitchEnvelopeDepth = readByte(curOffset++);
      uint8_t pitchEnvelopeRate = readByte(curOffset++);

      desc = fmt::format("Delay: {:d}  Depth: {:d}  Rate: {:d}", pitchEnvelopeDelay, pitchEnvelopeDepth, pitchEnvelopeRate);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope", desc, Type::PitchEnvelope);
      break;
    }

    case EVENT_PITCH_ENVELOPE_ON_OFF: {
      bool pitchEnvelopeOn = readByte(curOffset++) != 0;

      desc = fmt::format("Pitch Envelope: {}", pitchEnvelopeOn ? "On" : "Off");
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope On/Off", desc, Type::PitchEnvelope);
      break;
    }

    case EVENT_ADSR: {
      uint8_t adsr1 = readByte(curOffset++);
      uint8_t adsr2 = readByte(curOffset++);
	    spcADSR = adsr1 | (adsr2 << 8);
      desc = fmt::format("ADSR(1): ${:02X}  ADSR(2): ${:02X}", adsr1, adsr2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      break;
    }

    case EVENT_GAIN: {
      // Note: This command doesn't work properly on Ys V
      uint8_t newGAIN = readByte(curOffset++);

      desc = fmt::format("GAIN: ${:02X}", newGAIN);
      addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN", desc, Type::Adsr);
      break;
    }

    case EVENT_NOISE_FREQ: {
      uint8_t newNCK = readByte(curOffset++) & 0x1f;

      desc = fmt::format("Noise Frequency (NCK): {:d}", newNCK);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Frequency", desc, Type::Noise);
      break;
    }

    case EVENT_PITCHMOD: {
      bool pitchModOn = (readByte(curOffset++) != 0);

      desc = fmt::format("Pitch Modulation: {}", pitchModOn ? "On" : "Off");
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo", desc, Type::Modulation);
      break;
    }

    case EVENT_ECHO: {
      bool echoOn = (readByte(curOffset++) != 0);

      desc = fmt::format("Echo Write: {}", echoOn ? "On" : "Off");
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo", desc, Type::Reverb);
      addReverbNoItem(echoOn ? 40 : 0);
      break;
    }

    case EVENT_ECHO_PARAM: {
      uint8_t spcEDL = readByte(curOffset++);
      uint8_t spcEFB = readByte(curOffset++);
      uint8_t spcFIR = readByte(curOffset++);

      desc = fmt::format("Delay: {:d}  Feedback: {:d}  FIR: {:d}", spcEDL, spcEFB, spcFIR);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Param", desc, Type::Reverb);

      // enable echo for the channel
      addReverbNoItem(40);
      break;
    }

    case EVENT_ECHO_VOLUME_ON_OFF: {
      bool echoVolumeOn = readByte(curOffset++) != 0;

      desc = fmt::format("Echo Volume: {}", echoVolumeOn ? "On" : "Off");
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume On/Off", desc, Type::Reverb);
      break;
    }

    case EVENT_ECHO_VOLUME: {
      int8_t echoVolumeLeft = readByte(curOffset++);
      int8_t echoVolumeRight = readByte(curOffset++);

      desc = fmt::format("Echo Volume Left: {:d}  Echo Volume Right: {:d}", echoVolumeLeft, echoVolumeRight);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume", desc, Type::Reverb);
      break;
    }

    case EVENT_ECHO_FIR_OVERWRITE: {
      uint8_t presetNo = readByte(curOffset++);
      curOffset += 8; // FIR C0-C7

      desc = fmt::format("Preset: #{:d}", presetNo);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo FIR Overwrite", desc, Type::Reverb);
      break;
    }

    case EVENT_GOTO: {
      int16_t destOffset = readShort(curOffset);
      curOffset += 2;

      if (destOffset == 0) {
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
      }
      else {
        uint16_t dest = curOffset + destOffset;
        desc = fmt::format("Destination: ${:04X}", dest);
        uint32_t length = curOffset - beginOffset;

        curOffset = dest;
        if (!isOffsetUsed(dest)) {
          addGenericEvent(beginOffset, length, "Jump", desc, Type::LoopForever);
        }
        else {
          bContinue = addLoopForever(beginOffset, length, "Jump");
        }
      }
      break;
    }

    default:
      auto description = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", description);
      bContinue = false;
      break;
  }

  //std::ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}
