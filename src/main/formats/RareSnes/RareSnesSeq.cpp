#include "RareSnesSeq.h"
#include "ScaleConversion.h"
#include "spdlog/fmt/fmt.h"

using namespace std;

DECLARE_FORMAT(RareSnes);

//  **********
//  RareSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    32
#define SEQ_KEYOFS  36

const uint16_t RareSnesSeq::NOTE_PITCH_TABLE[128] = {
    0x0000, 0x0040, 0x0044, 0x0048, 0x004c, 0x0051, 0x0055, 0x005b,
    0x0060, 0x0066, 0x006c, 0x0072, 0x0079, 0x0080, 0x0088, 0x0090,
    0x0098, 0x00a1, 0x00ab, 0x00b5, 0x00c0, 0x00cb, 0x00d7, 0x00e4,
    0x00f2, 0x0100, 0x010f, 0x011f, 0x0130, 0x0143, 0x0156, 0x016a,
    0x0180, 0x0196, 0x01af, 0x01c8, 0x01e3, 0x0200, 0x021e, 0x023f,
    0x0261, 0x0285, 0x02ab, 0x02d4, 0x02ff, 0x032d, 0x035d, 0x0390,
    0x03c7, 0x0400, 0x043d, 0x047d, 0x04c2, 0x050a, 0x0557, 0x05a8,
    0x05fe, 0x065a, 0x06ba, 0x0721, 0x078d, 0x0800, 0x087a, 0x08fb,
    0x0984, 0x0a14, 0x0aae, 0x0b50, 0x0bfd, 0x0cb3, 0x0d74, 0x0e41,
    0x0f1a, 0x1000, 0x10f4, 0x11f6, 0x1307, 0x1429, 0x155c, 0x16a1,
    0x17f9, 0x1966, 0x1ae9, 0x1c82, 0x1e34, 0x2000, 0x21e7, 0x23eb,
    0x260e, 0x2851, 0x2ab7, 0x2d41, 0x2ff2, 0x32cc, 0x35d1, 0x3904,
    0x3c68, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff,
    0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff,
    0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff,
    0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff, 0x3fff
};

RareSnesSeq::RareSnesSeq(RawFile *file, RareSnesVersion ver, uint32_t seqdataOffset, string newName)
    : VGMSeq(RareSnesFormat::name, file, seqdataOffset, 0, newName), version(ver) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

RareSnesSeq::~RareSnesSeq(void) {
}

void RareSnesSeq::resetVars(void) {
  VGMSeq::resetVars();

  midiReverb = 40;
  switch (version) {
    case RARESNES_DKC:
      timerFreq = 0x3c;
      break;
    default:
      timerFreq = 0x64;
      break;
  }
  tempo = initialTempo;
  tempoBPM = getTempoInBPM(tempo, timerFreq);
  setAlwaysWriteInitialTempo(tempoBPM);
}

bool RareSnesSeq::parseHeader(void) {
  setPPQN(SEQ_PPQN);

  VGMHeader *seqHeader = addHeader(dwOffset, MAX_TRACKS * 2 + 2, "Sequence Header");
  uint32_t curHeaderOffset = dwOffset;
  for (int i = 0; i < MAX_TRACKS; i++) {
    uint16_t trkOff = readShort(curHeaderOffset);
    seqHeader->addPointer(curHeaderOffset, 2, trkOff, (trkOff != 0), "Track Pointer");
    curHeaderOffset += 2;
  }
  initialTempo = readByte(curHeaderOffset);
  seqHeader->addTempo(curHeaderOffset++, 1, "Tempo");
  seqHeader->addUnknownChild(curHeaderOffset++, 1);

  return true;        //successful
}


bool RareSnesSeq::parseTrackPointers(void) {
  for (int i = 0; i < MAX_TRACKS; i++) {
    uint16_t trkOff = readShort(dwOffset + i * 2);
    if (trkOff != 0)
      aTracks.push_back(new RareSnesTrack(this, trkOff));
  }
  return true;
}

void RareSnesSeq::loadEventMap() {
  // common events
  EventMap[0x00] = EVENT_END;
  EventMap[0x01] = EVENT_PROGCHANGE;
  EventMap[0x02] = EVENT_VOLLR;
  EventMap[0x03] = EVENT_GOTO;
  EventMap[0x04] = EVENT_CALLNTIMES;
  EventMap[0x05] = EVENT_RET;
  EventMap[0x06] = EVENT_DEFDURON;
  EventMap[0x07] = EVENT_DEFDUROFF;
  EventMap[0x08] = EVENT_PITCHSLIDEUP;
  EventMap[0x09] = EVENT_PITCHSLIDEDOWN;
  EventMap[0x0a] = EVENT_PITCHSLIDEOFF;
  EventMap[0x0b] = EVENT_TEMPO;
  EventMap[0x0c] = EVENT_TEMPOADD;
  EventMap[0x0d] = EVENT_VIBRATOSHORT;
  EventMap[0x0e] = EVENT_VIBRATOOFF;
  EventMap[0x0f] = EVENT_VIBRATO;
  EventMap[0x10] = EVENT_ADSR;
  EventMap[0x11] = EVENT_MASTVOLLR;
  EventMap[0x12] = EVENT_TUNING;
  EventMap[0x13] = EVENT_TRANSPABS;
  EventMap[0x14] = EVENT_TRANSPREL;
  EventMap[0x15] = EVENT_ECHOPARAM;
  EventMap[0x16] = EVENT_ECHOON;
  EventMap[0x17] = EVENT_ECHOOFF;
  EventMap[0x18] = EVENT_ECHOFIR;
  EventMap[0x19] = EVENT_NOISECLK;
  EventMap[0x1a] = EVENT_NOISEON;
  EventMap[0x1b] = EVENT_NOISEOFF;
  EventMap[0x1c] = EVENT_SETALTNOTE1;
  EventMap[0x1d] = EVENT_SETALTNOTE2;
  EventMap[0x26] = EVENT_PITCHSLIDEDOWNSHORT;
  EventMap[0x27] = EVENT_PITCHSLIDEUPSHORT;
  EventMap[0x2b] = EVENT_LONGDURON;
  EventMap[0x2c] = EVENT_LONGDUROFF;

  switch (version) {
    case RARESNES_DKC:
      EventMap[0x1c] = EVENT_SETVOLADSRPRESET1;
      EventMap[0x1d] = EVENT_SETVOLADSRPRESET2;
      EventMap[0x1e] = EVENT_SETVOLADSRPRESET3;
      EventMap[0x1f] = EVENT_SETVOLADSRPRESET4;
      EventMap[0x20] = EVENT_SETVOLADSRPRESET5;
      EventMap[0x21] = EVENT_GETVOLADSRPRESET1;
      EventMap[0x22] = EVENT_GETVOLADSRPRESET2;
      EventMap[0x23] = EVENT_GETVOLADSRPRESET3;
      EventMap[0x24] = EVENT_GETVOLADSRPRESET4;
      EventMap[0x25] = EVENT_GETVOLADSRPRESET5;
      EventMap[0x28] = EVENT_PROGCHANGEVOL;
      EventMap[0x29] = EVENT_UNKNOWN1;
      EventMap[0x2a] = EVENT_TIMERFREQ;
      EventMap[0x2d] = EVENT_CONDJUMP;
      EventMap[0x2e] = EVENT_SETCONDJUMPPARAM;
      EventMap[0x2f] = EVENT_TREMOLO;
      EventMap[0x30] = EVENT_TREMOLOOFF;
      break;

    case RARESNES_KI:
      //removed common events
      EventMap.erase(0x0c);
      EventMap.erase(0x0d);
      EventMap.erase(0x11);
      EventMap.erase(0x15);
      EventMap.erase(0x18);
      EventMap.erase(0x19);
      EventMap.erase(0x1a);
      EventMap.erase(0x1b);
      EventMap.erase(0x1c);
      EventMap.erase(0x1d);

      EventMap[0x1e] = EVENT_VOLCENTER;
      EventMap[0x1f] = EVENT_CALLONCE;
      EventMap[0x20] = EVENT_RESETADSR;
      EventMap[0x21] = EVENT_RESETADSRSOFT;
      EventMap[0x22] = EVENT_VOICEPARAMSHORT;
      EventMap[0x23] = EVENT_ECHODELAY;
      //EventMap[0x24] = null;
      //EventMap[0x25] = null;
      //EventMap[0x28] = null;
      //EventMap[0x29] = null;
      //EventMap[0x2a] = null;
      //EventMap[0x2d] = null;
      //EventMap[0x2e] = null;
      //EventMap[0x2f] = null;
      //EventMap[0x30] = null;
      break;

    case RARESNES_DKC2:
      //removed common events
      EventMap.erase(0x11);

      EventMap[0x1e] = EVENT_SETVOLPRESETS;
      EventMap[0x1f] = EVENT_ECHODELAY;
      EventMap[0x20] = EVENT_GETVOLPRESET1;
      EventMap[0x21] = EVENT_CALLONCE;
      EventMap[0x22] = EVENT_VOICEPARAM;
      EventMap[0x23] = EVENT_VOLCENTER;
      EventMap[0x24] = EVENT_MASTVOL;
      //EventMap[0x25] = null;
      //EventMap[0x28] = null;
      //EventMap[0x29] = null;
      //EventMap[0x2a] = null;
      //EventMap[0x2d] = null;
      //EventMap[0x2e] = null;
      //EventMap[0x2f] = null;
      EventMap[0x30] = EVENT_ECHOOFF; // duplicated
      EventMap[0x31] = EVENT_GETVOLPRESET2;
      EventMap[0x32] = EVENT_ECHOOFF; // duplicated
      break;

    case RARESNES_WNRN:
      //removed common events
      EventMap.erase(0x19);
      EventMap.erase(0x1a);
      EventMap.erase(0x1b);

      //EventMap[0x1e] = null;
      //EventMap[0x1f] = null;
      EventMap[0x20] = EVENT_MASTVOL;
      EventMap[0x21] = EVENT_VOLCENTER;
      EventMap[0x22] = EVENT_UNKNOWN3;
      EventMap[0x23] = EVENT_CALLONCE;
      EventMap[0x24] = EVENT_LFOOFF;
      EventMap[0x25] = EVENT_UNKNOWN4;
      EventMap[0x28] = EVENT_PROGCHANGEVOL;
      EventMap[0x29] = EVENT_UNKNOWN1;
      EventMap[0x2a] = EVENT_TIMERFREQ;
      //EventMap[0x2d] = null;
      //EventMap[0x2e] = null;
      EventMap[0x2f] = EVENT_TREMOLO;
      EventMap[0x30] = EVENT_TREMOLOOFF;
      //EventMap[0x31] = EVENT_RESET;
      break;
    default:
      L_WARN("Unknown version of Rare SNES format");
      break;
  }
}

double RareSnesSeq::getTempoInBPM(uint8_t tempo, uint8_t timerFreq) {
  if (timerFreq != 0 && tempo != 0) {
    return (double) 60000000 / (SEQ_PPQN * (125 * timerFreq)) * ((double) tempo / 256);
  }
  else {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}


//  ************
//  RareSnesTrack
//  ************

RareSnesTrack::RareSnesTrack(RareSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void RareSnesTrack::resetVars(void) {
  SeqTrack::resetVars();

  cKeyCorrection = SEQ_KEYOFS;

  rptNestLevel = 0;
  spcNotePitch = 0;
  spcTranspose = 0;
  spcTransposeAbs = 0;
  spcInstr = 0;
  spcADSR = 0x8EE0;
  spcTuning = 0;
  defNoteDur = 0;
  useLongDur = false;
  altNoteByte1 = altNoteByte2 = 0x81;
}


double RareSnesTrack::getTuningInSemitones(int8_t tuning) {
  return 12.0 * log((1024 + tuning) / 1024.0) / log(2.0);
}

void RareSnesTrack::calculateVolPanFromVolLR(int8_t volL, int8_t volR, uint8_t &midiVol, uint8_t &midiPan) {
  double volumeLeft = abs(volL) / 128.0;
  double volumeRight = abs(volR) / 128.0;

  double volumeScale;
  uint8_t midiPanTemp = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &volumeScale);
  volumeScale *= sqrt(3) / 2.0; // limit to <= 1.0

  midiVol = convertPercentAmpToStdMidiVal(volumeScale);
  if (volL != 0 || volR != 0) {
    midiPan = midiPanTemp;
  }
}

bool RareSnesTrack::readEvent(void) {
  RareSnesSeq *parentSeq = (RareSnesSeq *) this->parentSeq;
  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  uint8_t newMidiVol, newMidiPan;
  bool bContinue = true;

  std::string desc;

  if (statusByte >= 0x80) {
    uint8_t noteByte = statusByte;

    // check for "reuse last key"
    if (noteByte == 0xe1) {
      noteByte = altNoteByte2;
    }
    else if (noteByte >= 0xe0) {
      noteByte = altNoteByte1;
    }

    uint8_t key = noteByte - 0x81;
    uint8_t spcKey = min(max(noteByte - 0x80 + 36 + spcTranspose, 0), 0x7f);

    uint16_t dur;
    if (defNoteDur != 0) {
      dur = defNoteDur;
    }
    else {
      if (useLongDur) {
        dur = getShortBE(curOffset);
        curOffset += 2;
      }
      else {
        dur = readByte(curOffset++);
      }
    }

    if (noteByte == 0x80) {
      //ostringstream ssTrace;
      //ssTrace << "Rest: " << dur << " " << defNoteDur << " " << (useLongDur ? "L" : "S") << std::endl;
      //LogDebug(ssTrace.str().c_str());

      addRest(beginOffset, curOffset - beginOffset, dur);
    }
    else {
      // a note, add hints for instrument
      int8_t instrTuningDelta = 0;
      if (parentSeq->instrUnityKeyHints.find(spcInstr) == parentSeq->instrUnityKeyHints.end()) {
        parentSeq->instrUnityKeyHints[spcInstr] = spcTransposeAbs;
        parentSeq->instrPitchHints[spcInstr] = (int16_t)std::round(getTuningInSemitones(spcTuning) * 100.0);
      }
      else {
        // check difference between preserved tuning and current tuning
        // example case: Donkey Kong Country 2 - Forest Interlude (Pads)
        instrTuningDelta = spcTransposeAbs - parentSeq->instrUnityKeyHints[spcInstr];
      }
      if (parentSeq->instrADSRHints.find(spcInstr) == parentSeq->instrADSRHints.end()) {
        parentSeq->instrADSRHints[spcInstr] = spcADSR;
      }

      spcNotePitch = RareSnesSeq::NOTE_PITCH_TABLE[spcKey];
      spcNotePitch = (spcNotePitch * (1024 + spcTuning) + (spcTuning < 0 ? 1023 : 0)) / 1024;

      //ostringstream ssTrace;
      //ssTrace << "Note: " << key << " " << dur << " " << defNoteDur << " " << (useLongDur ? "L" : "S") << " P=" << spcNotePitch << std::endl;
      //LogDebug(ssTrace.str().c_str());

      uint8_t vel = 127;
      addNoteByDur(beginOffset, curOffset - beginOffset, key + instrTuningDelta, vel, dur);
      addTime(dur);
    }
  }
  else {
    RareSnesSeqEventType eventType = (RareSnesSeqEventType) 0;
    map<uint8_t, RareSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
    if (pEventType != parentSeq->EventMap.end()) {
      eventType = pEventType->second;
    }

    switch (eventType) {
      case EVENT_UNKNOWN0: {
        auto desc = describeUnknownEvent(statusByte);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

      case EVENT_UNKNOWN1: {
        const int arg1 = readByte(curOffset++);
        auto desc = describeUnknownEvent(statusByte, arg1);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

      case EVENT_UNKNOWN2: {
        const int arg1 = readByte(curOffset++);
        const int arg2 = readByte(curOffset++);
        auto desc = describeUnknownEvent(statusByte, arg1, arg2);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

      case EVENT_UNKNOWN3: {
        const int arg1 = readByte(curOffset++);
        const int arg2 = readByte(curOffset++);
        const int arg3 = readByte(curOffset++);
        auto desc = describeUnknownEvent(statusByte, arg1, arg2, arg3);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

      case EVENT_UNKNOWN4: {
        const int arg1 = readByte(curOffset++);
        const int arg2 = readByte(curOffset++);
        const int arg3 = readByte(curOffset++);
        const int arg4 = readByte(curOffset++);
        auto desc = describeUnknownEvent(statusByte, arg1, arg2, arg3, arg4);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
        break;
      }

      case EVENT_END:
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
        //loaded = true;
        break;

      case EVENT_PROGCHANGE: {
        uint8_t newProg = readByte(curOffset++);
        spcInstr = newProg;
        addProgramChange(beginOffset, curOffset - beginOffset, newProg, true);
        break;
      }

      case EVENT_PROGCHANGEVOL: {
        uint8_t newProg = readByte(curOffset++);
        int8_t newVolL = (int8_t) readByte(curOffset++);
        int8_t newVolR = (int8_t) readByte(curOffset++);

        spcInstr = newProg;
        spcVolL = newVolL;
        spcVolR = newVolR;
        addProgramChange(beginOffset, curOffset - beginOffset, newProg, true, "Program Change, Volume");
        addVolLRNoItem(spcVolL, spcVolR);
        break;
      }

      case EVENT_VOLLR: {
        int8_t newVolL = (int8_t) readByte(curOffset++);
        int8_t newVolR = (int8_t) readByte(curOffset++);

        spcVolL = newVolL;
        spcVolR = newVolR;
        addVolLR(beginOffset, curOffset - beginOffset, spcVolL, spcVolR, "Volume L/R");
        break;
      }

      case EVENT_VOLCENTER: {
        int8_t newVol = (int8_t) readByte(curOffset++);

        spcVolL = newVol;
        spcVolR = newVol;
        addVolLR(beginOffset, curOffset - beginOffset, spcVolL, spcVolR, "Volume");
        break;
      }

      case EVENT_GOTO: {
        const uint16_t dest = readShort(curOffset);
        curOffset += 2;
        auto desc = fmt::format("Destination: ${:04X}", dest);
        const uint32_t length = curOffset - beginOffset;

        curOffset = dest;
        if (!isOffsetUsed(dest) || rptNestLevel != 0)  // nest level check is required for Stickerbrush Symphony
          addGenericEvent(beginOffset, length, "Jump", desc, Type::LoopForever);
        else
          bContinue = addLoopForever(beginOffset, length, "Jump");
        break;
      }

      case EVENT_CALLNTIMES: {
        const int times = readByte(curOffset++);
        const uint16_t dest = readShort(curOffset);
        curOffset += 2;

        auto desc = fmt::format("Times: {:d}  Destination: ${:04X}", times, dest);
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        (times == 1 ? "Pattern Play" : "Pattern Repeat"), desc,
                        Type::RepeatStart);

        if (rptNestLevel == RARESNES_RPTNESTMAX) {
          L_ERROR("Subroutine nest level overflow");
          bContinue = false;
          break;
        }

        rptRetnAddr[rptNestLevel] = curOffset;
        rptCount[rptNestLevel] = times;
        rptStart[rptNestLevel] = dest;
        rptNestLevel++;
        curOffset = dest;
        break;
      }

      case EVENT_CALLONCE: {
        const uint16_t dest = readShort(curOffset);
        curOffset += 2;

        auto desc = fmt::format("Destination: ${:04X}", dest);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern Play",
                        desc, Type::RepeatStart);

        if (rptNestLevel == RARESNES_RPTNESTMAX) {
          L_ERROR("Subroutine nest level overflow");
          bContinue = false;
          break;
        }

        rptRetnAddr[rptNestLevel] = curOffset;
        rptCount[rptNestLevel] = 1;
        rptStart[rptNestLevel] = dest;
        rptNestLevel++;
        curOffset = dest;
        break;
      }

      case EVENT_RET: {
        addGenericEvent(beginOffset, curOffset - beginOffset, "End Pattern", "",
                        Type::RepeatEnd);

        if (rptNestLevel == 0) {
          L_ERROR("Subroutine nest level overflow");
          bContinue = false;
          break;
        }

        rptNestLevel--;
        rptCount[rptNestLevel] = (rptCount[rptNestLevel] - 1) & 0xff;
        if (rptCount[rptNestLevel] != 0) {
          // continue
          curOffset = rptStart[rptNestLevel];
          rptNestLevel++;
        }
        else {
          // return
          curOffset = rptRetnAddr[rptNestLevel];
        }
        break;
      }

      case EVENT_DEFDURON: {
        if (useLongDur) {
          defNoteDur = getShortBE(curOffset);
          curOffset += 2;
        }
        else {
          defNoteDur = readByte(curOffset++);
        }

        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Default Duration On", "", Type::DurationChange);
        break;
      }

      case EVENT_DEFDUROFF:
        defNoteDur = 0;
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Default Duration Off", "", Type::DurationChange);
        break;

      case EVENT_PITCHSLIDEUP: {
        curOffset += 5;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide Up",
                        "", Type::PitchBendSlide);
        break;
      }

      case EVENT_PITCHSLIDEDOWN: {
        curOffset += 5;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide Down",
                        "", Type::PitchBendSlide);
        break;
      }

      case EVENT_PITCHSLIDEOFF:
        addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide Off",
                        "", Type::PitchBendSlide);
        break;

      case EVENT_TEMPO: {
        uint8_t newTempo = readByte(curOffset++);
        parentSeq->tempo = newTempo;
        addTempoBPM(beginOffset,
                    curOffset - beginOffset,
                    parentSeq->getTempoInBPM(parentSeq->tempo, parentSeq->timerFreq));
        break;
      }

      case EVENT_TEMPOADD: {
        int8_t deltaTempo = (int8_t) readByte(curOffset++);
        parentSeq->tempo = (parentSeq->tempo + deltaTempo) & 0xff;
        addTempoBPM(beginOffset,
                    curOffset - beginOffset,
                    parentSeq->getTempoInBPM(parentSeq->tempo, parentSeq->timerFreq),
                    "Tempo Add");
        break;
      }

      case EVENT_VIBRATOSHORT: {
        curOffset += 3;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato (Short)",
                        "", Type::Vibrato);
        break;
      }

      case EVENT_VIBRATOOFF:
        addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Off", "",
                        Type::Vibrato);
        break;

      case EVENT_VIBRATO: {
        curOffset += 4;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", "",
                        Type::Vibrato);
        break;
      }

      case EVENT_TREMOLOOFF:
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Off", "",
                        Type::Tremelo);
        break;

      case EVENT_TREMOLO: {
        curOffset += 4;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo", "",
                        Type::Tremelo);
        break;
      }

      case EVENT_ADSR: {
        uint16_t newADSR = getShortBE(curOffset);
        curOffset += 2;
        spcADSR = newADSR;

        auto desc = fmt::format("ADSR: {:04X}", newADSR);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
        break;
      }

      case EVENT_MASTVOL: {
        // TODO: At least it's not Master Volume in Donkey Kong Country 2
        uint8_t newVol = readByte(curOffset++);
        auto desc = fmt::format("Volume: {:d}", newVol);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Master Volume?",
                        desc, Type::MasterVolume);
        break;
      }

      case EVENT_MASTVOLLR: {
        int8_t newVolL = (int8_t) readByte(curOffset++);
        int8_t newVolR = (int8_t) readByte(curOffset++);
        int8_t newVol = min(abs((int) newVolL) + abs((int) newVolR), 255) / 2; // workaround: convert to mono
        addMasterVol(beginOffset, curOffset - beginOffset, newVol, "Master Volume L/R");
        break;
      }

      case EVENT_TUNING: {
        int8_t newTuning = static_cast<int8_t>(readByte(curOffset++));
        spcTuning = newTuning;
        auto desc = fmt::format("Tuning: {:d} ({:d} cents)", newTuning,
                                int(getTuningInSemitones(newTuning) * 100 + 0.5));
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tuning", desc, Type::FineTune);
        break;
      }

      case EVENT_TRANSPABS: // should be used for pitch correction of instrument
      {
        int8_t newTransp = static_cast<int8_t>(readByte(curOffset++));
        spcTranspose = spcTransposeAbs = newTransp;
        //AddTranspose(beginOffset, curOffset-beginOffset, 0, "Transpose (Abs)");

        auto desc = fmt::format("Transpose: {:d}", newTransp);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Transpose", desc, Type::Transpose);

        cKeyCorrection = SEQ_KEYOFS;
        break;
      }

      case EVENT_TRANSPREL: {
        int8_t deltaTransp = static_cast<int8_t>(readByte(curOffset++));
        spcTranspose = (spcTranspose + deltaTransp) & 0xff;
        //AddTranspose(beginOffset, curOffset-beginOffset, spcTransposeAbs - spcTranspose, "Transpose (Rel)");

        auto desc = fmt::format("Transpose: {:d}", deltaTransp);
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Transpose (Relative)", desc, Type::Transpose);

        cKeyCorrection += deltaTransp;
        break;
      }

      case EVENT_ECHOPARAM: {
        uint8_t newFeedback = readByte(curOffset++);
        int8_t newVolL = (int8_t) readByte(curOffset++);
        int8_t newVolR = (int8_t) readByte(curOffset++);
        parentSeq->midiReverb = min(abs((int) newVolL) + abs((int) newVolR), 255) / 2;
        // TODO: update MIDI reverb value for each tracks?

        auto desc = fmt::format("Feedback: {:d}  Volume: {:d}, {:d}",
                                      newFeedback, newVolL, newVolR);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Param", desc, Type::Reverb);
        break;
      }

      case EVENT_ECHOON:
        addReverb(beginOffset, curOffset - beginOffset, parentSeq->midiReverb, "Echo On");
        break;

      case EVENT_ECHOOFF:
        addReverb(beginOffset, curOffset - beginOffset, 0, "Echo Off");
        break;

      case EVENT_ECHOFIR: {
        uint8_t newFIR[8];
        readBytes(curOffset, 8, newFIR);
        curOffset += 8;

        std::string desc = fmt::format("Filter: {:02X}", newFIR[0]);
        for (int iFIRIndex = 1; iFIRIndex < 8; iFIRIndex++) {
          desc += fmt::format(" {:02X}", newFIR[iFIRIndex]);
        }

        addGenericEvent(beginOffset, curOffset - beginOffset, "Echo FIR", desc, Type::Reverb);
        break;
      }

      case EVENT_NOISECLK: {
        uint8_t newCLK = readByte(curOffset++);
        auto desc = fmt::format("CLK: {:d}", newCLK);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Frequency",
                        desc, Type::Noise);
        break;
      }

      case EVENT_NOISEON:
        addGenericEvent(beginOffset, curOffset - beginOffset, "Noise On", "",
                        Type::Noise);
        break;

      case EVENT_NOISEOFF:
        addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Off", "",
                        Type::Noise);
        break;

      case EVENT_SETALTNOTE1:
        altNoteByte1 = readByte(curOffset++);
        desc = fmt::format("Note: {:02X}", altNoteByte1);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Alt Note 1",
                        desc, Type::ChangeState);
        break;

      case EVENT_SETALTNOTE2:
        altNoteByte2 = readByte(curOffset++);
        desc = fmt::format("Note: {:02X}", altNoteByte2);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Alt Note 2",
                        desc, Type::ChangeState);
        break;

      case EVENT_PITCHSLIDEDOWNSHORT: {
        curOffset += 4;
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Pitch Slide Down (Short)", "", Type::PitchBendSlide);
        break;
      }

      case EVENT_PITCHSLIDEUPSHORT: {
        curOffset += 4;
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Pitch Slide Up (Short)", "", Type::PitchBendSlide);
        break;
      }

      case EVENT_LONGDURON:
        useLongDur = true;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Long Duration On",
                        "", Type::DurationChange);
        break;

      case EVENT_LONGDUROFF:
        useLongDur = false;
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Long Duration Off", "", Type::DurationChange);
        break;

      case EVENT_SETVOLADSRPRESET1: {
        int8_t newVolL = (int8_t) readByte(curOffset++);
        int8_t newVolR = (int8_t) readByte(curOffset++);
        uint8_t adsr1 = readByte(curOffset++);
        uint8_t adsr2 = readByte(curOffset++);

        uint8_t ar = adsr1 & 0x0f;
        uint8_t dr = (adsr1 & 0x70) >> 4;
        uint8_t sl = (adsr2 & 0xe0) >> 5;
        uint8_t sr = adsr2 & 0x1f;

        parentSeq->presetVolL[0] = newVolL;
        parentSeq->presetVolR[0] = newVolR;
        parentSeq->presetADSR[0] = (adsr1 << 8) | adsr2;

        // add event without MIDI events
        calculateVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);
        desc = fmt::format("Left Volume: {:d}  Right Volume: {:d}  AR: {:d}  DR: {:d}  SL: {:d}  SR: {:d}",
                           newVolL, newVolR, ar, dr, sl, sr);
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Set Vol/ADSR Preset 1", desc, Type::Volume);
        break;
      }

      case EVENT_SETVOLADSRPRESET2: {
        int8_t newVolL = (int8_t) readByte(curOffset++);
        int8_t newVolR = (int8_t) readByte(curOffset++);
        uint8_t adsr1 = readByte(curOffset++);
        uint8_t adsr2 = readByte(curOffset++);

        uint8_t ar = adsr1 & 0x0f;
        uint8_t dr = (adsr1 & 0x70) >> 4;
        uint8_t sl = (adsr2 & 0xe0) >> 5;
        uint8_t sr = adsr2 & 0x1f;

        parentSeq->presetVolL[1] = newVolL;
        parentSeq->presetVolR[1] = newVolR;
        parentSeq->presetADSR[1] = (adsr1 << 8) | adsr2;

        // add event without MIDI events
        calculateVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);
        desc = fmt::format("Left Volume: {:d}  Right Volume: {:d}  AR: {:d}  DR: {:d}  SL: {:d}  SR: {:d}",
                           newVolL, newVolR, ar, dr, sl, sr);
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Set Vol/ADSR Preset 2", desc, Type::Volume);
        break;
      }

      case EVENT_SETVOLADSRPRESET3: {
        int8_t newVolL = (int8_t) readByte(curOffset++);
        int8_t newVolR = (int8_t) readByte(curOffset++);
        uint8_t adsr1 = readByte(curOffset++);
        uint8_t adsr2 = readByte(curOffset++);

        uint8_t ar = adsr1 & 0x0f;
        uint8_t dr = (adsr1 & 0x70) >> 4;
        uint8_t sl = (adsr2 & 0xe0) >> 5;
        uint8_t sr = adsr2 & 0x1f;

        parentSeq->presetVolL[2] = newVolL;
        parentSeq->presetVolR[2] = newVolR;
        parentSeq->presetADSR[2] = (adsr1 << 8) | adsr2;

        // add event without MIDI events
        calculateVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);
        desc = fmt::format("Left Volume: {:d}  Right Volume: {:d}  AR: {:d}  DR: {:d}  SL: {:d}  SR: {:d}",
                           newVolL, newVolR, ar, dr, sl, sr);
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Set Vol/ADSR Preset 3", desc, Type::Volume);
        break;
      }

      case EVENT_SETVOLADSRPRESET4: {
        int8_t newVolL = (int8_t) readByte(curOffset++);
        int8_t newVolR = (int8_t) readByte(curOffset++);
        uint8_t adsr1 = readByte(curOffset++);
        uint8_t adsr2 = readByte(curOffset++);

        uint8_t ar = adsr1 & 0x0f;
        uint8_t dr = (adsr1 & 0x70) >> 4;
        uint8_t sl = (adsr2 & 0xe0) >> 5;
        uint8_t sr = adsr2 & 0x1f;

        parentSeq->presetVolL[3] = newVolL;
        parentSeq->presetVolR[3] = newVolR;
        parentSeq->presetADSR[3] = (adsr1 << 8) | adsr2;

        // add event without MIDI events
        calculateVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);
        desc = fmt::format("Left Volume: {:d}  Right Volume: {:d}  AR: {:d}  DR: {:d}  SL: {:d}  SR: {:d}",
                           newVolL, newVolR, ar, dr, sl, sr);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Vol/ADSR Preset 4", desc, Type::Volume);
        break;
      }

      case EVENT_SETVOLADSRPRESET5: {
        int8_t newVolL = (int8_t) readByte(curOffset++);
        int8_t newVolR = (int8_t) readByte(curOffset++);
        uint8_t adsr1 = readByte(curOffset++);
        uint8_t adsr2 = readByte(curOffset++);

        uint8_t ar = adsr1 & 0x0f;
        uint8_t dr = (adsr1 & 0x70) >> 4;
        uint8_t sl = (adsr2 & 0xe0) >> 5;
        uint8_t sr = adsr2 & 0x1f;

        parentSeq->presetVolL[4] = newVolL;
        parentSeq->presetVolR[4] = newVolR;
        parentSeq->presetADSR[4] = (adsr1 << 8) | adsr2;

        // add event without MIDI events
        calculateVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);
        desc = fmt::format("Left Volume: {:d}  Right Volume: {:d}  AR: {:d}  DR: {:d}  SL: {:d}  SR: {:d}",
                           newVolL, newVolR, ar, dr, sl, sr);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Vol/ADSR Preset 5", desc, Type::Volume);
        break;
      }

      case EVENT_GETVOLADSRPRESET1:
        spcVolL = parentSeq->presetVolL[0];
        spcVolR = parentSeq->presetVolR[0];
        addVolLR(beginOffset, curOffset - beginOffset, spcVolL, spcVolR, "Get Vol/ADSR Preset 1");
        break;

      case EVENT_GETVOLADSRPRESET2:
        spcVolL = parentSeq->presetVolL[1];
        spcVolR = parentSeq->presetVolR[1];
        addVolLR(beginOffset, curOffset - beginOffset, spcVolL, spcVolR, "Get Vol/ADSR Preset 2");
        break;

      case EVENT_GETVOLADSRPRESET3:
        spcVolL = parentSeq->presetVolL[2];
        spcVolR = parentSeq->presetVolR[2];
        addVolLR(beginOffset, curOffset - beginOffset, spcVolL, spcVolR, "Get Vol/ADSR Preset 3");
        break;

      case EVENT_GETVOLADSRPRESET4:
        spcVolL = parentSeq->presetVolL[3];
        spcVolR = parentSeq->presetVolR[3];
        addVolLR(beginOffset, curOffset - beginOffset, spcVolL, spcVolR, "Get Vol/ADSR Preset 4");
        break;

      case EVENT_GETVOLADSRPRESET5:
        spcVolL = parentSeq->presetVolL[4];
        spcVolR = parentSeq->presetVolR[4];
        addVolLR(beginOffset, curOffset - beginOffset, spcVolL, spcVolR, "Get Vol/ADSR Preset 5");
        break;

      case EVENT_TIMERFREQ: {
        uint8_t newFreq = readByte(curOffset++);
        parentSeq->timerFreq = newFreq;
        addTempoBPM(beginOffset,
                    curOffset - beginOffset,
                    parentSeq->getTempoInBPM(parentSeq->tempo, parentSeq->timerFreq),
                    "Timer Frequency");
        break;
      }

        //case EVENT_CONDJUMP:
        //	break;

        //case EVENT_SETCONDJUMPPARAM:
        //	break;

      case EVENT_RESETADSR:
        spcADSR = 0x8FE0;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Reset ADSR", "ADSR: 8FE0", Type::Adsr);
        break;

      case EVENT_RESETADSRSOFT:
        spcADSR = 0x8EE0;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Reset ADSR (Soft)",
                        "ADSR: 8EE0",
                        Type::Adsr);
        break;

      case EVENT_VOICEPARAMSHORT: {
        uint8_t newProg = readByte(curOffset++);
        int8_t newTransp = (int8_t) readByte(curOffset++);
        int8_t newTuning = (int8_t) readByte(curOffset++);

        desc = fmt::format(
            "Program Number: {:d}  Transpose: {:d}  Tuning: {:d} ({:d} cents)",
            newProg, newTransp, newTuning,
            int(getTuningInSemitones(newTuning) * 100 + 0.5));

        // instrument
        spcInstr = newProg;
        addProgramChange(beginOffset, curOffset - beginOffset, newProg, true,
                         "Program Change, Transpose, Tuning");

        // transpose
        spcTranspose = spcTransposeAbs = newTransp;
        cKeyCorrection = SEQ_KEYOFS;

        // tuning
        spcTuning = newTuning;
        break;
      }

      case EVENT_VOICEPARAM: {
        uint8_t newProg = readByte(curOffset++);
        int8_t newTransp = (int8_t) readByte(curOffset++);
        int8_t newTuning = (int8_t) readByte(curOffset++);
        int8_t newVolL = (int8_t) readByte(curOffset++);
        int8_t newVolR = (int8_t) readByte(curOffset++);
        uint16_t newADSR = getShortBE(curOffset);
        curOffset += 2;

        desc = fmt::format(
            "Program Number: {:d}  Transpose: {:d}  Tuning: {:d} ({:d} cents)  Volume: {:d}, {:d}  ADSR: {:04X}",
            newProg, newTransp, newTuning,
            int(getTuningInSemitones(newTuning) * 100 + 0.5), newVolL, newVolR,
            newADSR);

        // instrument
        spcInstr = newProg;
        addProgramChange(beginOffset,
                         curOffset - beginOffset,
                         newProg,
                         true,
                         "Program Change, Transpose, Tuning, Volume L/R, ADSR");

        // transpose
        spcTranspose = spcTransposeAbs = newTransp;
        cKeyCorrection = SEQ_KEYOFS;

        // tuning
        spcTuning = newTuning;

        // volume
        spcVolL = newVolL;
        spcVolR = newVolR;
        addVolLRNoItem(spcVolL, spcVolR);

        // ADSR
        spcADSR = newADSR;

        break;
      }

      case EVENT_ECHODELAY: {
        uint8_t newEDL = readByte(curOffset++);
        auto desc = fmt::format("Delay: {:d}", newEDL);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Delay", desc, Type::Reverb);
        break;
      }

      case EVENT_SETVOLPRESETS: {
        int8_t newVolL1 = (int8_t) readByte(curOffset++);
        int8_t newVolR1 = (int8_t) readByte(curOffset++);
        int8_t newVolL2 = (int8_t) readByte(curOffset++);
        int8_t newVolR2 = (int8_t) readByte(curOffset++);

        parentSeq->presetVolL[0] = newVolL1;
        parentSeq->presetVolR[0] = newVolR1;
        parentSeq->presetVolL[1] = newVolL2;
        parentSeq->presetVolR[1] = newVolR2;

        // add event without MIDI events
        calculateVolPanFromVolLR(parentSeq->presetVolL[0], parentSeq->presetVolR[0], newMidiVol, newMidiPan);
        desc = fmt::format(
            "Left Volume 1: {:d}  Right Volume 1: {:d}  Left Volume 2: {:d}  Right Volume 2: {:d}",
            newVolL1, newVolR1, newVolL2, newVolR2);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Volume Preset",
                        desc, Type::Control);
        break;
      }

      case EVENT_GETVOLPRESET1:
        spcVolL = parentSeq->presetVolL[0];
        spcVolR = parentSeq->presetVolR[0];
        addVolLR(beginOffset, curOffset - beginOffset, spcVolL, spcVolR, "Get Volume Preset 1");
        break;

      case EVENT_GETVOLPRESET2:
        spcVolL = parentSeq->presetVolL[1];
        spcVolR = parentSeq->presetVolR[1];
        addVolLR(beginOffset, curOffset - beginOffset, spcVolL, spcVolR, "Get Volume Preset 2");
        break;

      case EVENT_LFOOFF:
        addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide/Vibrato/Tremolo Off", "",
                        Type::ChangeState);
        break;

      default: {
        auto descr = logEvent(statusByte);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
        bContinue = false;
        break;
      }
    }
  }

  //ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //LogDebug(ssTrace.str().c_str());

  return bContinue;
}

void RareSnesTrack::onTickBegin(void) {
}

void RareSnesTrack::onTickEnd(void) {
}

void RareSnesTrack::addVolLR(uint32_t offset,
                             uint32_t length,
                             int8_t spcVolL,
                             int8_t spcVolR,
                             const std::string &sEventName) {
  uint8_t newMidiVol;
  uint8_t newMidiPan;
  calculateVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);

  auto desc = fmt::format("Left Volume: {:d}  Right Volume: {:d}", spcVolL, spcVolR);
  addGenericEvent(offset, length, sEventName, desc, Type::Volume);

  // add MIDI events only if updated
  if (newMidiVol != vol) {
    addVolNoItem(newMidiVol);
  }
  if (newMidiVol != 0 && newMidiPan != prevPan) {
    addPanNoItem(newMidiPan);
  }
}

void RareSnesTrack::addVolLRNoItem(int8_t spcVolL, int8_t spcVolR) {
  uint8_t newMidiVol;
  uint8_t newMidiPan;
  calculateVolPanFromVolLR(spcVolL, spcVolR, newMidiVol, newMidiPan);

  // add MIDI events only if updated
  if (newMidiVol != vol) {
    addVolNoItem(newMidiVol);
  }
  if (newMidiVol != 0 && newMidiPan != prevPan) {
    addPanNoItem(newMidiPan);
  }
}
