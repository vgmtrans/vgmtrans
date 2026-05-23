/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include <sstream>
#include "CapcomSnesSeq.h"
#include "CapcomSnesDefinitions.h"
#include "Modulation.h"
#include "ScaleConversion.h"
#include "automation/SeqTrackAutomation.h"

DECLARE_FORMAT(CapcomSnes);

//  **********
//  CapcomSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    48
#define SEQ_KEYOFS  0

// volume table
const uint8_t CapcomSnesSeq::volTable[] = {
    0x00, 0x0c, 0x19, 0x26, 0x33, 0x40, 0x4c, 0x59,
    0x66, 0x73, 0x80, 0x8c, 0x99, 0xb3, 0xcc, 0xe6,
    0xff,
};

// pan table (compatible with Nintendo engine)
const uint8_t CapcomSnesSeq::panTable[] = {
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
    0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
    0x7a, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f,
};

CapcomSnesSeq::CapcomSnesSeq(RawFile *file,
                             CapcomSnesVersion ver,
                             uint32_t seqdataOffset,
                             bool priorityInHeader,
                             std::string name)
    : VGMSeq(CapcomSnesFormat::name, file, seqdataOffset, 0, std::move(name)), version(ver),
      priorityInHeader(priorityInHeader) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);
  setAlwaysWriteInitialMonoMode(true);

  loadEventMap();
}

void CapcomSnesSeq::resetVars() {
  VGMSeq::resetVars();

  midiReverb = 40;
  transpose = 0;
}

bool CapcomSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  VGMHeader *seqHeader = addHeader(offset(), (priorityInHeader ? 1 : 0) + MAX_TRACKS * 2, "Sequence Header");
  uint32_t curHeaderOffset = offset();

  if (priorityInHeader) {
    seqHeader->addChild(curHeaderOffset, 1, "Priority");
    curHeaderOffset++;
  }

  for (int i = 0; i < MAX_TRACKS; i++) {
    uint16_t trkOff = readShortBE(curHeaderOffset);
    seqHeader->addPointer(curHeaderOffset, 2, trkOff, true, "Track Pointer");
    curHeaderOffset += 2;
  }

  return true;
}

bool CapcomSnesSeq::parseTrackPointers() {
  for (int i = MAX_TRACKS - 1; i >= 0; i--) {
    uint16_t trkOff = readShortBE(offset() + (priorityInHeader ? 1 : 0) + i * 2);
    if (trkOff != 0)
      aTracks.push_back(new CapcomSnesTrack(this, trkOff));
  }
  return true;
}

void CapcomSnesSeq::loadEventMap() {
  EventMap[0x00] = EVENT_TOGGLE_TRIPLET;
  EventMap[0x01] = EVENT_TOGGLE_SLUR;
  EventMap[0x02] = EVENT_DOTTED_NOTE_ON;
  EventMap[0x03] = EVENT_TOGGLE_OCTAVE_UP;
  EventMap[0x04] = EVENT_NOTE_ATTRIBUTES;
  EventMap[0x05] = EVENT_TEMPO;
  EventMap[0x06] = EVENT_DURATION;
  EventMap[0x07] = EVENT_VOLUME;
  EventMap[0x08] = EVENT_PROGRAM_CHANGE;
  EventMap[0x09] = EVENT_OCTAVE;
  EventMap[0x0a] = EVENT_GLOBAL_TRANSPOSE;
  EventMap[0x0b] = EVENT_TRANSPOSE;
  EventMap[0x0c] = EVENT_TUNING;
  EventMap[0x0d] = EVENT_PORTAMENTO_TIME;
  EventMap[0x0e] = EVENT_REPEAT_UNTIL_1;
  EventMap[0x0f] = EVENT_REPEAT_UNTIL_2;
  EventMap[0x10] = EVENT_REPEAT_UNTIL_3;
  EventMap[0x11] = EVENT_REPEAT_UNTIL_4;
  EventMap[0x12] = EVENT_REPEAT_BREAK_1;
  EventMap[0x13] = EVENT_REPEAT_BREAK_2;
  EventMap[0x14] = EVENT_REPEAT_BREAK_3;
  EventMap[0x15] = EVENT_REPEAT_BREAK_4;
  EventMap[0x16] = EVENT_GOTO;
  EventMap[0x17] = EVENT_END;
  EventMap[0x18] = EVENT_PAN;
  EventMap[0x19] = EVENT_MASTER_VOLUME;
  EventMap[0x1a] = EVENT_LFO;
  EventMap[0x1b] = EVENT_ECHO_PARAM;
  EventMap[0x1c] = EVENT_ECHO_ONOFF;
  EventMap[0x1d] = EVENT_RELEASE_RATE;
  EventMap[0x1e] = EVENT_NOP;
  EventMap[0x1f] = EVENT_NOP;

  switch (version) {
    case CAPCOMSNES_V1_BGM_IN_LIST:
      EventMap[0x1e] = EVENT_UNKNOWN1;
      EventMap[0x1f] = EVENT_UNKNOWN1;
      break;

    default:
      break;
  }
}

double CapcomSnesSeq::getTempoInBPM() const {
  return getTempoInBPM(tempo);
}

double CapcomSnesSeq::getTempoInBPM(uint16_t tempo) {
  if (tempo != 0) {
    return 60000000.0 / (SEQ_PPQN * (125 * 0x40) * 2) * (tempo / 256.0);
  }
  else {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}


//  ************
//  CapcomSnesTrack
//  ************

CapcomSnesTrack::CapcomSnesTrack(CapcomSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  CapcomSnesTrack::resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void CapcomSnesTrack::resetVars() {
  SeqTrack::resetVars();

  cKeyCorrection = SEQ_KEYOFS;

  noteAttributes = 0;
  durationRate = 0;
  transpose = 0;
  lastNoteSlurred = false;
  didRest = false;
  lastKey = -1;
  vibrato.reset();
  tremolo.reset();
  lastPortamentoTime = 0;
  portamentoMillisecondsPerCent = 0;

  for (uint8_t& i : repeatCount) {
    i = 0;
  }
}

void CapcomSnesTrack::setLfoOutputsEnabled(bool enabled) {
  if (vibrato.depth() != 0) {
    emitVibratoDepth(vibrato, vibrato.outputDepthWhen(enabled));
  }
  if (tremolo.depth() != 0) {
    emitTremoloDepth(tremolo, tremolo.outputDepthWhen(enabled));
  }
}

void CapcomSnesTrack::addVibratoDepthEvent(uint32_t offset, uint32_t length, uint8_t depth) {
  bool isNewOffset = onEvent(offset, length);
  vibrato.setDepth(depth);

  // Record the stored depth, but emit zero while the driver has the LFO disabled.
  recordSeqEvent<ModulationSeqEvent>(isNewOffset, getTime(), depth, offset, length, "Vibrato Depth");
  emitVibratoDepth(vibrato, vibrato.outputDepthWhen(areLfoOutputsEnabled()), true);
}

void CapcomSnesTrack::handleLfoRateChange(uint8_t lfoRateByte) {
  const bool wasEnabled = areLfoOutputsEnabled();
  vibrato.setRate(lfoRateByte);
  tremolo.setRate(lfoRateByte);
  const bool isEnabled = areLfoOutputsEnabled();

  // Rate 0 gates the active LFOs off without clearing the stored depths.
  if (!isEnabled && wasEnabled) {
    setLfoOutputsEnabled(false);
  }
  else if (isEnabled && !wasEnabled) {
    // Reactivate vibrato and tremolo if appropriate
    setLfoOutputsEnabled(true);
  }
}


uint8_t CapcomSnesTrack::getNoteOctave() const {
  return noteAttributes & CAPCOM_SNES_MASK_NOTE_OCTAVE;
}

void CapcomSnesTrack::setNoteOctave(uint8_t octave) {
  noteAttributes = (noteAttributes & ~CAPCOM_SNES_MASK_NOTE_OCTAVE) | (octave & CAPCOM_SNES_MASK_NOTE_OCTAVE);
}

bool CapcomSnesTrack::isNoteOctaveUp() const {
  return (noteAttributes & CAPCOM_SNES_MASK_NOTE_OCTAVE_UP) != 0;
}

void CapcomSnesTrack::setNoteOctaveUp(bool octave_up) {
  if (octave_up) {
    noteAttributes |= CAPCOM_SNES_MASK_NOTE_OCTAVE_UP;
  }
  else {
    noteAttributes &= ~CAPCOM_SNES_MASK_NOTE_OCTAVE_UP;
  }
}

bool CapcomSnesTrack::isNoteDotted() const {
  return (noteAttributes & CAPCOM_SNES_MASK_NOTE_DOTTED) != 0;
}

void CapcomSnesTrack::setNoteDotted(bool dotted) {
  if (dotted) {
    noteAttributes |= CAPCOM_SNES_MASK_NOTE_DOTTED;
  }
  else {
    noteAttributes &= ~CAPCOM_SNES_MASK_NOTE_DOTTED;
  }
}

bool CapcomSnesTrack::isNoteTriplet() const {
  return (noteAttributes & CAPCOM_SNES_MASK_NOTE_TRIPLET) != 0;
}

void CapcomSnesTrack::setNoteTriplet(bool triplet) {
  if (triplet) {
    noteAttributes |= CAPCOM_SNES_MASK_NOTE_TRIPLET;
  }
  else {
    noteAttributes &= ~CAPCOM_SNES_MASK_NOTE_TRIPLET;
  }
}

bool CapcomSnesTrack::isNoteSlurred() const {
  return (noteAttributes & CAPCOM_SNES_MASK_NOTE_SLURRED) != 0;
}

void CapcomSnesTrack::setNoteSlurred(bool slurred) {
  if (slurred) {
    noteAttributes |= CAPCOM_SNES_MASK_NOTE_SLURRED;
  }
  else {
    noteAttributes &= ~CAPCOM_SNES_MASK_NOTE_SLURRED;
  }
}

double CapcomSnesTrack::getTuningInSemitones(int8_t tuning) {
  return tuning / 256.0;
}
namespace {

constexpr int kVolumeCurveLastIndex = 16;
constexpr int kTremoloPeakScalarV1 = 255;
constexpr int kTremoloPeakScalarV2 = 250;
constexpr double kTremoloMuteFloorCentibels = 960.0;

struct PanConversionResult {
  uint8_t midiPan;
  double volumeScale;
};

int interpolatePanFactor(uint16_t panPosition) {
  const int panIndex = panPosition >> 8;
  const int panRate = panPosition & 0xff;
  const int lower = CapcomSnesSeq::panTable[panIndex];
  const int upper = CapcomSnesSeq::panTable[panIndex + 1];
  return lower + ((upper - lower) * panRate >> 8);
}

PanConversionResult calculatePanV2(uint8_t biasedPan) {
  const uint16_t rightPanPosition = static_cast<uint16_t>(biasedPan) * 20;
  const uint16_t leftPanPosition = 0x1400 - rightPanPosition;
  const double volumeLeft = interpolatePanFactor(leftPanPosition) / 128.0;
  const double volumeRight = interpolatePanFactor(rightPanPosition) / 128.0;

  PanConversionResult result{};
  result.midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &result.volumeScale);
  return result;
}

int interpolateVolumeCurve(int curveIndex, int curveFraction) {
  if (curveIndex >= kVolumeCurveLastIndex) {
    return CapcomSnesSeq::volTable[kVolumeCurveLastIndex]; // 0xff
  }

  const int lower = CapcomSnesSeq::volTable[curveIndex];
  const int upper = CapcomSnesSeq::volTable[curveIndex + 1];
  return lower + (((upper - lower) * curveFraction) >> 8);
}

int calculateVolumeScalar(uint8_t sourceVolume) {
  // The driver's intended range is 0x00..0x80 with 0x80 resolving to full volume.
  if (sourceVolume >= 0x80) {
    return CapcomSnesSeq::volTable[kVolumeCurveLastIndex]; // 0xff
  }
  const int curveIndex = sourceVolume >> 3;
  const int curveFraction = ((sourceVolume & 0x07) << 5) | 0x1f;

  return interpolateVolumeCurve(curveIndex, curveFraction);
}

double calculateVolumeV2(uint8_t sourceVolume) {
  const int scalar = calculateVolumeScalar(sourceVolume); // 0..255
  return static_cast<double>(scalar) / 255.0;
}

int calculateTremoloScalarAtTroughV1(int sourceDepth) {
  const int depth = sourceDepth & 0x7f;

  if (depth == 0)
    return 255;

  return 255 - ((2 * depth * 255) >> 8);
}

int calculateTremoloScalarAtTroughV2(int sourceDepth) {
  // The V2 handler uses the volume curve table
  sourceDepth = std::clamp(sourceDepth, 0, 127);

  if (sourceDepth == 0)
    return 250;
  if (sourceDepth == 127)
    return 0;

  // Tremolo depth walks the same loudness curve as volume, but in reverse.
  const int inverseCurvePosition = 0x7E81 - sourceDepth * 255;
  const int scaledCurvePosition = inverseCurvePosition >> 3;
  return interpolateVolumeCurve(scaledCurvePosition >> 8, scaledCurvePosition & 0xff);
}

int convertTremoloDepthToMidiValue(int sourceDepth, CapcomSnesVersion version) {
  int peakScalar;
  int troughScalar;

  if (version == CAPCOMSNES_V1_BGM_IN_LIST) {
    peakScalar = kTremoloPeakScalarV1;
    troughScalar = calculateTremoloScalarAtTroughV1(sourceDepth);
  }
  else {
    peakScalar = kTremoloPeakScalarV2;
    troughScalar = calculateTremoloScalarAtTroughV2(sourceDepth);
  }
  double depthCentibels = kTremoloMuteFloorCentibels;

  if (troughScalar > 0) {
    depthCentibels = 200.0 * log10(peakScalar / static_cast<double>(troughScalar));
    depthCentibels = std::clamp(depthCentibels, 0.0, kTremoloMuteFloorCentibels);
  }

  // SF2 tremolo depth is expressed as +/- modLfoToVolume, so the MIDI value must map to the full range, not just half.
  const int midiValue = static_cast<int>(
      floor(depthCentibels * 128.0 / (2.0 * static_cast<double>(capcom_snes::kTremoloHalfDepthCentibels)) + 0.5));
  return std::clamp(midiValue, 0, 127);
}

uint8_t convertLfoRateByteToMidiVal(uint8_t freqByte) {
  if (freqByte == 0)
    return 0; // this is a special-case that disables vibrato

  // The driver stores rate as a multiple of a fixed LFO step; the shared helper
  // handles the Hz -> 7-bit MIDI mapping that matches the instrument modulator.
  return midiValueForHertzInRange(static_cast<double>(freqByte) * capcom_snes::kLfoStepHz,
                                  capcom_snes::kVibratoBaseHz,
                                  capcom_snes::kVibratoMaxHz);
}

}  // namespace

bool CapcomSnesTrack::readEvent() {
  CapcomSnesSeq *parentSeq = static_cast<CapcomSnesSeq*>(this->parentSeq);
  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;
  auto applyNoteAttributes = [this](uint8_t attributes) {
    const bool wasSlurred = isNoteSlurred();
    noteAttributes &= ~(CAPCOM_SNES_MASK_NOTE_OCTAVE_UP | CAPCOM_SNES_MASK_NOTE_TRIPLET | CAPCOM_SNES_MASK_NOTE_SLURRED);
    noteAttributes |= attributes;

    if (isNoteSlurred() != wasSlurred) {
      addLegatoPedalNoItem(isNoteSlurred());
    }
  };

  if (statusByte >= 0x20) {
    uint8_t keyIndex = statusByte & 0x1f;
    uint8_t lenIndex = statusByte >> 5;
    bool rest = (keyIndex == 0);

    // calcurate actual note length:
    // actual music engine acquires the length from a table,
    // but it can be calculated quite easily. Here it is.
    uint8_t len = 192 >> (7 - lenIndex);
    if (isNoteDotted()) {
      if (len % 2 == 0 && len < 0x80) {
        len = len + (len / 2);
      }
      else {
        // error: note length is not a byte value.
        len = 0;
        L_WARN("Note length overflow");
      }
      setNoteDotted(false);
    }
    else if (isNoteTriplet()) {
      len = len * 2 / 3;
    }

    if (rest) {
      addRest(beginOffset, curOffset - beginOffset, len);
      didRest = true;
    }
    else {
      // calculate duration of note:
      // actual music engine does it in 16 bit precision, because of tempo handling.
      uint16_t dur = len * durationRate;

      if (isNoteSlurred()) {
        // slurred/tied note must be full-length.
        dur = len << 8;
      }
      else {
        // if 0, it will probably become full-length,
        // because the music engine thinks the note is already off (release time).
        if (dur == 0) {
          dur = len << 8;
        }
      }

      // duration - bit reduction for MIDI
      dur = (dur + 0x80) >> 8;
      if (dur == 0) {
        dur = 1;
      }

      uint8_t key = (keyIndex - 1) + (getNoteOctave() * 12) + (isNoteOctaveUp() ? 24 : 0);
      uint8_t vel = 127;
      if (lastNoteSlurred && key == lastKey && !didRest) {
        addTime(dur);
        makePrevDurNoteEnd();
        addTime(len - dur);
        addTie(beginOffset, curOffset - beginOffset, dur, "Tie", desc);
      }
      else {
        if (portamentoMillisecondsPerCent > 0 && lastKey >= 0) {
          uint16_t portamentoDurInMillis = (abs(key - lastKey) * 100) * portamentoMillisecondsPerCent;
          if (portamentoDurInMillis != lastPortamentoTime) {
            addPortamentoTime14BitNoItem(portamentoDurInMillis);
            lastPortamentoTime = portamentoDurInMillis;
          }
          addPortamentoControlNoItem(lastKey);
        }
        addNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur + (isNoteSlurred() ? 1 : 0));
        addTime(len);
        lastKey = key;
        didRest = false;
      }
      lastNoteSlurred = isNoteSlurred();
    }
  }
  else {
    CapcomSnesSeqEventType eventType = static_cast<CapcomSnesSeqEventType>(0);
    std::map<uint8_t, CapcomSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
    if (pEventType != parentSeq->EventMap.end()) {
      eventType = pEventType->second;
    }

    switch (eventType) {
      case EVENT_UNKNOWN0: {
        auto descr = describeUnknownEvent(statusByte);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
        break;
      }

      case EVENT_UNKNOWN1: {
        uint8_t arg1 = readByte(curOffset++);
        auto descr = describeUnknownEvent(statusByte, arg1);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
        break;
      }

      case EVENT_UNKNOWN2: {
        uint8_t arg1 = readByte(curOffset++);
        uint8_t arg2 = readByte(curOffset++);
        auto descr = describeUnknownEvent(statusByte, arg1, arg2);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
        break;
      }

      case EVENT_UNKNOWN3: {
        uint8_t arg1 = readByte(curOffset++);
        uint8_t arg2 = readByte(curOffset++);
        uint8_t arg3 = readByte(curOffset++);
        auto descr = describeUnknownEvent(statusByte, arg1, arg2, arg3);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
        break;
      }

      case EVENT_UNKNOWN4: {
        uint8_t arg1 = readByte(curOffset++);
        uint8_t arg2 = readByte(curOffset++);
        uint8_t arg3 = readByte(curOffset++);
        uint8_t arg4 = readByte(curOffset++);
        auto descr = describeUnknownEvent(statusByte, arg1, arg2, arg3, arg4);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
        break;
      }

      case EVENT_TOGGLE_TRIPLET:
        setNoteTriplet(!isNoteTriplet());
        addGenericEvent(beginOffset, curOffset - beginOffset, "Toggle Triplet", "", Type::DurationChange);
        break;

      case EVENT_TOGGLE_SLUR:
        setNoteSlurred(!isNoteSlurred());
        addLegatoPedalNoItem(isNoteSlurred());
        addGenericEvent(beginOffset, curOffset - beginOffset, "Toggle Slur/Tie", "", Type::Portamento);
        break;

      case EVENT_DOTTED_NOTE_ON:
        setNoteDotted(true);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Dotted Note On", "", Type::DurationChange);
        break;

      case EVENT_TOGGLE_OCTAVE_UP:
        setNoteOctaveUp(!isNoteOctaveUp());
        addGenericEvent(beginOffset, curOffset - beginOffset, "Toggle 2-Octave Up", "", Type::Octave);
        break;

      case EVENT_NOTE_ATTRIBUTES: {
        uint8_t attributes = readByte(curOffset++);
        applyNoteAttributes(attributes);
        desc = fmt::format("Triplet: {}  Slur: {}  2-Octave Up: {}",
                        isNoteTriplet() ? "On" : "Off",
                        isNoteSlurred() ? "On" : "Off",
                        isNoteOctaveUp() ? "On" : "Off");
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Note Attributes",
                        desc,
                        Type::ChangeState);
        break;
      }

      case EVENT_TEMPO: {
        uint16_t newTempo = getShortBE(curOffset);
        curOffset += 2;
        parentSeq->tempo = newTempo;
        addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM());
        break;
      }

      case EVENT_DURATION: {
        uint8_t newDurationRate = readByte(curOffset++);
        durationRate = newDurationRate;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Duration",
                        fmt::format("Duration: {:d}/256", newDurationRate),
                        Type::DurationChange);
        break;
      }

      case EVENT_VOLUME: {
        uint8_t newVolume = readByte(curOffset++);

        if (parentSeq->version == CAPCOMSNES_V1_BGM_IN_LIST) {
          // linear volume
          addVol(beginOffset, curOffset - beginOffset, newVolume >> 1);
        }
        else {
          // V2/V3 drivers shape volume through the engine's 17-point loudness curve.
          double percentAmp = calculateVolumeV2(newVolume);
          addVol(beginOffset, curOffset - beginOffset, percentAmp, Resolution::FourteenBit);
        }
        break;
      }

      case EVENT_PROGRAM_CHANGE: {
        uint8_t newProg = readByte(curOffset++);
        addProgramChange(beginOffset, curOffset - beginOffset, newProg, true);
        break;
      }

      case EVENT_OCTAVE: {
        uint8_t newOctave = readByte(curOffset++);
        desc = fmt::format("Octave: {}", newOctave);
        setNoteOctave(newOctave);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Octave", desc, Type::Octave);
        break;
      }

      case EVENT_GLOBAL_TRANSPOSE: {
        int8_t newTranspose = readByte(curOffset++);
        addGlobalTranspose(beginOffset, curOffset - beginOffset, newTranspose);
        break;
      }

      case EVENT_TRANSPOSE: {
        int8_t newTranspose = readByte(curOffset++);
        addTranspose(beginOffset, curOffset - beginOffset, newTranspose);
        break;
      }

      case EVENT_TUNING: {
        int8_t newTuning = static_cast<int8_t>(readByte(curOffset++));
        double cents = getTuningInSemitones(newTuning) * 100.0;
        addFineTuning(beginOffset, curOffset - beginOffset, cents);
        break;
      }

      case EVENT_PORTAMENTO_TIME: {
        // All tested versions of the format (V1-V3) use the same calculation for portamento time.
        uint8_t portamentoTimeByte = readByte(curOffset++);
        uint8_t step = (portamentoTimeByte << 1) & 0xFF;
        double centsPerUpdate = step * (100.0 / 256.0);
        // The voice stream/portamento update runs once every other 8 ms timer tick, i.e. about 62.5 updates per second
        if (centsPerUpdate == 0)
          portamentoMillisecondsPerCent = 0;
        else
          portamentoMillisecondsPerCent = (0.016 / centsPerUpdate) * 1000;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Portamento Time",
                        fmt::format("cents/ms: {:.2f}", centsPerUpdate * 62.5 / 1000.0),
                        Type::PortamentoTime);
        break;
      }

      case EVENT_REPEAT_UNTIL_1:
      case EVENT_REPEAT_UNTIL_2:
      case EVENT_REPEAT_UNTIL_3:
      case EVENT_REPEAT_UNTIL_4: {
        uint8_t times = readByte(curOffset++);
        uint16_t dest = getShortBE(curOffset);
        curOffset += 2;

        uint8_t repeatSlot;
        const char* repeatEventName;
        switch (eventType) {
			case EVENT_REPEAT_UNTIL_1: repeatSlot = 0; repeatEventName = "Repeat Until #1"; break;
			case EVENT_REPEAT_UNTIL_2: repeatSlot = 1; repeatEventName = "Repeat Until #2"; break;
			case EVENT_REPEAT_UNTIL_3: repeatSlot = 2; repeatEventName = "Repeat Until #3"; break;
			case EVENT_REPEAT_UNTIL_4: repeatSlot = 3; repeatEventName = "Repeat Until #4"; break;
			default: break;
        }
        desc = fmt::format("Times: {:d}  Destination: ${:04X}", times, dest);
        if (times == 0 && repeatCount[repeatSlot] == 0) {
          // infinite loop
          bContinue = addLoopForever(beginOffset, curOffset - beginOffset, repeatEventName);

          if (readMode == READMODE_ADD_TO_UI) {
            if (readByte(curOffset) == 0x17) {
              addEndOfTrack(curOffset, 1);
            }
          }
        }
        else {
          // regular N-times loop
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          repeatEventName,
                          desc,
                          Type::RepeatEnd);
        }

        if (repeatCount[repeatSlot] == 0) {
          repeatCount[repeatSlot] = times;
          curOffset = dest;
        }
        else {
          repeatCount[repeatSlot]--;
          if (repeatCount[repeatSlot] != 0) {
            curOffset = dest;
          }
        }

        break;
      }

      case EVENT_REPEAT_BREAK_1:
      case EVENT_REPEAT_BREAK_2:
      case EVENT_REPEAT_BREAK_3:
      case EVENT_REPEAT_BREAK_4: {
        uint8_t attributes = readByte(curOffset++);
        uint16_t dest = getShortBE(curOffset);
        curOffset += 2;

        uint8_t repeatSlot;
        const char* repeatEventName;
        switch (eventType) {
			case EVENT_REPEAT_BREAK_1: repeatSlot = 0; repeatEventName = "Repeat Break #1"; break;
			case EVENT_REPEAT_BREAK_2: repeatSlot = 1; repeatEventName = "Repeat Break #2"; break;
			case EVENT_REPEAT_BREAK_3: repeatSlot = 2; repeatEventName = "Repeat Break #3"; break;
			case EVENT_REPEAT_BREAK_4: repeatSlot = 3; repeatEventName = "Repeat Break #4"; break;
			default: break;
        }
        desc = fmt::format("Note: {{ Triplet: {}  Slur: {}  2-Octave Up: {} }} Destination: ${:04X}",
                isNoteTriplet() ? "On" : "Off",
                isNoteSlurred() ? "On" : "Off",
                isNoteOctaveUp() ? "On" : "Off",
                dest);
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        repeatEventName,
                        desc,
                        Type::LoopBreak);

        if (repeatCount[repeatSlot] == 1) {
          repeatCount[repeatSlot] = 0;
          applyNoteAttributes(attributes);
          curOffset = dest;
        }

        break;
      }

      case EVENT_GOTO: {
        uint16_t dest = getShortBE(curOffset);
        curOffset += 2;
        desc = fmt::format("Destination: ${:04X}", dest);
        uint32_t length = curOffset - beginOffset;

        if (!isOffsetUsed(dest)) {
          addGenericEvent(beginOffset, length, "Jump", desc, Type::LoopForever);
        }
        else {
          bContinue = addLoopForever(beginOffset, length, "Jump");

          if (readMode == READMODE_ADD_TO_UI) {
            if (readByte(curOffset) == 0x17) {
              addEndOfTrack(curOffset, 1);
            }
          }
        }
        curOffset = dest;
        break;
      }

      case EVENT_END:
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
        break;

      case EVENT_PAN: {
        uint8_t newPan = readByte(curOffset++) + 0x80; // signed -> unsigned
        PanConversionResult pan{};
        if (parentSeq->version == CAPCOMSNES_V1_BGM_IN_LIST) {
          pan.midiPan = convert7bitLinearPercentPanValToStdMidiVal(newPan >> 1, &pan.volumeScale);
        }
        else {
          pan = calculatePanV2(newPan);
        }

        addPan(beginOffset, curOffset - beginOffset, pan.midiPan);
        addExpressionNoItem(std::round(127.0 * pan.volumeScale));
        break;
      }

      case EVENT_MASTER_VOLUME: {
        uint8_t newVolume = readByte(curOffset++);

        if (parentSeq->version == CAPCOMSNES_V1_BGM_IN_LIST) {
          // linear volume
          addMasterVol(beginOffset, curOffset - beginOffset, newVolume >> 1);
        }
        else {
          // Master volume follows the same loudness curve as per-track volume.
          double percentAmp = calculateVolumeV2(newVolume);
          addMasterVol(beginOffset, curOffset - beginOffset, percentAmp, Resolution::FourteenBit);
        }
        break;
      }

      case EVENT_LFO: {
        uint8_t lfoType = readByte(curOffset++);
        uint8_t lfoAmount = readByte(curOffset++);
        switch (lfoType) {
          case 0:
            // Vibrato Depth
            addVibratoDepthEvent(beginOffset, curOffset - beginOffset, lfoAmount & 0x7F);
            break;
          case 1:
            // Tremolo Depth
            tremolo.setDepth(convertTremoloDepthToMidiValue(lfoAmount, parentSeq->version));
            emitTremoloDepth(tremolo, tremolo.outputDepthWhen(areLfoOutputsEnabled()), true);
            desc = fmt::format("Amount: {:d}", lfoAmount);
            addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Depth", desc, Type::Lfo);
            break;
          case 2:
            // LFO Rate
            handleLfoRateChange(lfoAmount);
            addVibratoFrequency(beginOffset,
                                curOffset - beginOffset,
                                convertLfoRateByteToMidiVal(lfoAmount),
                                "LFO Rate");
            break;
          case 3:
            // Flag to reset LFO phase on note activation. 1 enables. 0 disables. V1: off by default, V2+: on by default
            // SoundFont 2 and DLS always reset LFO phase when a note is activated. This cannot be changed.
            desc = fmt::format("Type: {:d}  Amount: {:d}", lfoType, lfoAmount);
            addGenericEvent(beginOffset, curOffset - beginOffset, "Reset LFO at Note On", desc, Type::Lfo);
            break;
        }
        break;
      }

      case EVENT_ECHO_PARAM: {
        uint8_t echoArg1 = readByte(curOffset++);
        uint8_t echoPreset = readByte(curOffset++);
        desc = fmt::format("Arg1: {:d}  Preset: {:d}", echoArg1, echoPreset);
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Echo Param",
                        desc,
                        Type::Reverb);
        break;
      }

      case EVENT_ECHO_ONOFF: {
        if ((readByte(curOffset++) & 1) != 0) {
          addReverb(beginOffset, curOffset - beginOffset, parentSeq->midiReverb, "Echo On");
        }
        else {
          addReverb(beginOffset, curOffset - beginOffset, 0, "Echo Off");
        }
        break;
      }

      case EVENT_RELEASE_RATE: {
        uint8_t gain = readByte(curOffset++) | 0xa0;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Release Rate",
                        fmt::format("GAIN: ${:02X}", gain),
                        Type::Adsr);
        break;
      }

      default:
        auto description = logEvent(statusByte);
        addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", description);
        bContinue = false;
        break;
    }
  }

  //ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //LogDebug(ssTrace.str().c_str());

  return bContinue;
}

void CapcomSnesTrack::onTickBegin() {}

void CapcomSnesTrack::onTickEnd() {}
