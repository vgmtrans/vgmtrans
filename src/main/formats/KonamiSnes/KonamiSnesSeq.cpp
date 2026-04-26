/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiSnesSeq.h"
#include "KonamiSnesInstr.h"
#include "ScaleConversion.h"
#include "spdlog/fmt/fmt.h"
#include <algorithm>
#include <cmath>

DECLARE_FORMAT(KonamiSnes);

namespace {
constexpr uint16_t KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS = 200;
constexpr int16_t MIDI_PITCH_BEND_MIN = -8192;
constexpr int16_t MIDI_PITCH_BEND_MAX = 8191;
}

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
  bLoadTickByTick = true;

  setAlwaysWriteInitialVol(127);
  useReverb();
  setAlwaysWriteInitialReverb(0);
  setAlwaysWriteInitialPitchBendRange(KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS);

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

  VGMHeader *seqHeader = addHeader(offset(), nNumTracks * 2, "Sequence Header");
  for (uint32_t trackNumber = 0; trackNumber < nNumTracks; trackNumber++) {
    uint32_t trackPointerOffset = offset() + (trackNumber * 2);
    if (trackPointerOffset + 2 > 0x10000) {
      return false;
    }

    uint16_t trkOff = readShort(trackPointerOffset);
    seqHeader->addPointer(trackPointerOffset, 2, trkOff, true, "Track Pointer");

    assert(trkOff >= offset());

    if (trkOff - offset() < nNumTracks * 2) {
      nNumTracks = (trkOff - offset()) / 2;
    }
  }
  seqHeader->setLength(nNumTracks * 2);

  return true;
}


bool KonamiSnesSeq::parseTrackPointers(void) {
  for (uint32_t trackNumber = 0; trackNumber < nNumTracks; trackNumber++) {
    uint16_t trkOff = readShort(offset() + trackNumber * 2);
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
  EventMap[0xef] = EVENT_VOLUME_SLIDE_V1;
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
      EventMap[0xed] = EVENT_UNKNOWN3; // nop with 3 parameter bytes total
      EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V1;
      EventMap[0xf3] = EVENT_PITCH_SLIDE_V1;
      EventMap[0xfa] = EVENT_UNKNOWN3;
      EventMap[0xfb] = EVENT_UNKNOWN1;
      EventMap[0xfc] = EVENT_CONDITIONAL_JUMP_V1;
      break;

    case KONAMISNES_V2:
      EventMap[0xed] = EVENT_UNKNOWN3; // nop with 3 parameter bytes total
      EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V1;
      EventMap[0xf3] = EVENT_PITCH_SLIDE_V2;
      EventMap[0xfa] = EVENT_UNKNOWN3;
      EventMap[0xfb] = EVENT_UNKNOWN1;
      EventMap[0xfc] = EVENT_LINEAR_PITCH_ENVELOPE_V2;
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
      EventMap[0xef] = EVENT_VOLUME_SLIDE_V2;
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
      EventMap[0xef] = EVENT_VOLUME_SLIDE_V2;
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

  seqTuningCents = 0.0;

  volumeSlide = {};
  pitchSlide = {};
  pitchBendRangeCents = KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS;
  currentPitchBend = 0;
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

void KonamiSnesTrack::onTickBegin() {
  if (volumeSlide.useLength) {
    volumeSlide.length -= 1;
    if (volumeSlide.length == 0) {
      volumeSlide.currentVolume = volumeSlide.targetVolume;
      clearActiveVolumeSlide();
    }
    else {
      volumeSlide.currentVolume += volumeSlide.delta;
    }
    applyCurrentVolume();
  }
  else if (volumeSlide.delta != 0) {
    volumeSlide.currentVolume += volumeSlide.delta;
    if ((volumeSlide.delta > 0 && volumeSlide.currentVolume >= volumeSlide.targetVolume)
        || (volumeSlide.delta < 0 && volumeSlide.currentVolume <= volumeSlide.targetVolume)) {
      volumeSlide.currentVolume = volumeSlide.targetVolume;
      clearActiveVolumeSlide();
    }
    applyCurrentVolume();
  }

  if (pitchSlide.length == 0 || !pitchSlide.baseValid) {
    return;
  }

  if (pitchSlide.delay > 0) {
    pitchSlide.delay -= 1;
    return;
  }

  pitchSlide.length -= 1;
  if (pitchSlide.length == 0) {
    pitchSlide.currentSemitones = pitchSlide.targetSemitones;
  }
  else {
    pitchSlide.currentSemitones += pitchSlide.deltaSemitones;
  }

  applyCurrentPitchBend();
}

std::optional<KonamiSnesTrack::PitchSlide> KonamiSnesTrack::consumePitchSlide() {
  if (!isValidOffset(curOffset)) {
    return std::nullopt;
  }

  auto nextEvent = static_cast<KonamiSnesSeq*>(parentSeq)->EventMap.find(readByte(curOffset));
  if (nextEvent == static_cast<KonamiSnesSeq*>(parentSeq)->EventMap.end()) {
    return std::nullopt;
  }

  switch (nextEvent->second) {
    case EVENT_PITCH_SLIDE_V1:
    case EVENT_PITCH_SLIDE_V3:
      return readPitchSlide(nextEvent->second, curOffset++);

    default:
      return std::nullopt;
  }
}

KonamiSnesTrack::PitchSlide KonamiSnesTrack::readPitchSlide(KonamiSnesSeqEventType eventType, uint32_t offset) {
  PitchSlide slide {
    offset,
    0,
    readByte(curOffset++),
    readByte(curOffset++),
    readByte(curOffset++),
  };

  switch (eventType) {
    case EVENT_PITCH_SLIDE_V1:
      slide.eventLength = 4;
      slide.targetSemitones = noteSemitones(slide.targetNote, true);
      if (slide.length != 0 && pitchSlide.baseValid) {
        slide.delta = static_cast<int16_t>(((slide.targetSemitones - pitchSlide.currentSemitones) * 256.0)
                                           / slide.length);
        slide.deltaSemitones = slide.delta / 256.0;
      }
      break;

    case EVENT_PITCH_SLIDE_V3:
      slide.eventLength = 6;
      slide.targetSemitones = noteSemitones(slide.targetNote, false);
      slide.delta = static_cast<int16_t>(readShort(curOffset));
      slide.deltaSemitones = (slide.delta / 256.0)
                             * (256.0 / std::max<uint8_t>(static_cast<KonamiSnesSeq*>(parentSeq)->tempo, 1));
      curOffset += 2;
      break;

    default:
      assert(false);
      break;
  }
  return slide;
}

void KonamiSnesTrack::clearActivePitchSlide() {
  pitchSlide.delay = 0;
  pitchSlide.length = 0;
  pitchSlide.targetSemitones = 0.0;
  pitchSlide.deltaSemitones = 0.0;
}

void KonamiSnesTrack::addPitchSlideEvent(const PitchSlide& slide) {
  const uint8_t pitchSlideNoteNumber = (slide.targetNote & 0x7f) + transpose;
  const auto desc = fmt::format("Delay: {:d}  Length: {:d}  Final Note: {:d}  Delta: {:.1f} semitones",
                                slide.delay, slide.length, pitchSlideNoteNumber, slide.delta / 256.0);
  addGenericEvent(slide.offset, slide.eventLength, "Pitch Slide", desc, Type::PitchBendSlide);
}

double KonamiSnesTrack::noteSemitones(uint8_t key, bool includeTuning) const {
  double semitones = (key & 0x7f) + cKeyCorrection + transpose;
  if (includeTuning) {
    semitones += coarseTuningSemitones + (fineTuningCents / 100.0);
  }
  return semitones;
}

void KonamiSnesTrack::resetPitchForNote(uint8_t key) {
  clearActivePitchSlide();
  setPitchBend(0);
  pitchSlide.baseValid = !percussion;

  if (!pitchSlide.baseValid) {
    return;
  }

  pitchSlide.baseSemitones = noteSemitones(key, true);
  pitchSlide.currentSemitones = pitchSlide.baseSemitones;
}

uint16_t KonamiSnesTrack::pitchSlideRangeCents(const PitchSlide& slide) const {
  double maxSlideSemitones = std::abs(slide.targetSemitones - pitchSlide.baseSemitones);
  if (slide.length > 1) {
    maxSlideSemitones = std::max(maxSlideSemitones, std::abs((slide.length - 1) * slide.deltaSemitones));
  }

  return maxSlideSemitones > 2.0
      ? static_cast<uint16_t>(std::ceil(maxSlideSemitones) * 100.0)
      : KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS;
}

void KonamiSnesTrack::setPitchBendRange(uint16_t cents) {
  if (cents == 0 || cents == pitchBendRangeCents) {
    return;
  }

  addPitchBendRangeNoItem(cents);
  pitchBendRangeCents = cents;

  if (pitchSlide.baseValid) {
    applyCurrentPitchBend();
  }
}

void KonamiSnesTrack::setPitchBend(int16_t bend) {
  if (bend == currentPitchBend) {
    return;
  }

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->addPitchBend(channel, bend);
  }
  currentPitchBend = bend;
}

void KonamiSnesTrack::applyCurrentPitchBend() {
  const auto bend = static_cast<int32_t>(std::lround(((pitchSlide.currentSemitones - pitchSlide.baseSemitones) * 100.0
                                                      / pitchBendRangeCents) * 8192.0));
  setPitchBend(static_cast<int16_t>(std::clamp(bend,
                                               static_cast<int32_t>(MIDI_PITCH_BEND_MIN),
                                               static_cast<int32_t>(MIDI_PITCH_BEND_MAX))));
}

void KonamiSnesTrack::beginPitchSlide(const PitchSlide& slide) {
  clearActivePitchSlide();
  addPitchSlideEvent(slide);

  if (!pitchSlide.baseValid || slide.length == 0) {
    setPitchBendRange(KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS);
    return;
  }

  setPitchBendRange(pitchSlideRangeCents(slide));
  pitchSlide.delay = slide.delay;
  pitchSlide.length = slide.length;
  pitchSlide.targetSemitones = slide.targetSemitones;
  pitchSlide.deltaSemitones = slide.deltaSemitones;
}

KonamiSnesTrack::VolumeSlide KonamiSnesTrack::readVolumeSlide(KonamiSnesSeqEventType eventType, uint32_t offset) const {
  VolumeSlide slide {offset, 0};

  switch (eventType) {
    case EVENT_VOLUME_SLIDE_V1:
      slide.length = readByte(curOffset);
      slide.targetVolume = readByte(curOffset + 1);
      slide.useLength = true;
      if (slide.length != 0) {
        slide.delta = static_cast<int16_t>(((static_cast<int32_t>(slide.targetVolume) << 8)
                                            - (volumeSlide.currentVolume & 0xff00))
                                           / slide.length);
      }
      break;

    case EVENT_VOLUME_SLIDE_V2:
      slide.targetVolume = readByte(curOffset);
      slide.delta = static_cast<int16_t>(static_cast<int8_t>(readByte(curOffset + 1)) << 4);
      break;

    default:
      assert(false);
      break;
  }

  return slide;
}

void KonamiSnesTrack::addVolumeSlideEvent(const VolumeSlide& slide) {
  const std::string desc = slide.useLength
      ? fmt::format("Length: {:d}  Target Volume: {:d}", slide.length, slide.targetVolume)
      : fmt::format("Target Volume: {:d}  Speed: {:.2f}", slide.targetVolume, slide.delta / 256.0);
  addGenericEvent(slide.offset, 3, "Volume Slide", desc, Type::VolumeSlide);
}

void KonamiSnesTrack::clearActiveVolumeSlide() {
  volumeSlide.targetVolume = volumeSlide.currentVolume;
  volumeSlide.delta = 0;
  volumeSlide.length = 0;
  volumeSlide.useLength = false;
}

void KonamiSnesTrack::applyCurrentVolume() {
  const uint8_t rawVolume = static_cast<uint8_t>(std::clamp(volumeSlide.currentVolume >> 8, 0, 0xff));
  const uint8_t midiVolume = convertPercentAmpToStdMidiVal(rawVolume / 255.0);
  if (midiVolume != vol) {
    addVolNoItem(midiVolume);
  }
}

void KonamiSnesTrack::beginVolumeSlide(const VolumeSlide& slide) {
  addVolumeSlideEvent(slide);

  volumeSlide.currentVolume &= 0xff00;
  volumeSlide.targetVolume = slide.targetVolume << 8;
  volumeSlide.delta = slide.delta;
  volumeSlide.length = slide.length;
  volumeSlide.useLength = slide.useLength;

  if ((slide.useLength && slide.length == 0) || (!slide.useLength && slide.delta == 0)) {
    volumeSlide.currentVolume = volumeSlide.targetVolume;
    clearActiveVolumeSlide();
    applyCurrentVolume();
  }
}

int16_t KonamiSnesTrack::getLoopVolumeDelta() const {
  return loopVolumeDelta + loopVolumeDelta2;
}

double KonamiSnesTrack::getLoopPitchDeltaCents() const {
  // The loop opcodes store pitch deltas in units of 8/256 semitones.
  return static_cast<double>(loopPitchDelta + loopPitchDelta2) * (100.0 / 32.0);
}

void KonamiSnesTrack::applyEffectiveTuning(uint32_t offset, uint32_t length) {
  const double totalCents = seqTuningCents + getLoopPitchDeltaCents();
  const double desiredCoarse = std::trunc(totalCents / 100.0);
  const double desiredFine = totalCents - (desiredCoarse * 100.0);

  if (coarseTuningSemitones != desiredCoarse) {
    addCoarseTuningNoItem(desiredCoarse);
  }
  if (std::abs(fineTuningCents - desiredFine) > 0.001) {
    addFineTuningNoItem(desiredFine);
  }
}

bool KonamiSnesTrack::readEvent() {
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

      vel = static_cast<uint8_t>(std::clamp<int>(vel + getLoopVolumeDelta(), 1, 127));
      vel = convertPercentAmpToStdMidiVal(vel / 127.0);
      applyEffectiveTuning(beginOffset, curOffset - beginOffset);

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

      const uint32_t noteLengthBytes = curOffset - beginOffset;
      resetPitchForNote(key);
      const auto slide = consumePitchSlide();

      if (prevNoteSlurred && key == prevNoteKey) {
        // TODO: Note volume can be changed during a tied note
        // See the end of Konami Logo sequence for example
        makePrevDurNoteEnd(getTime() + dur);
        addTie(beginOffset, noteLengthBytes, dur, "Tie", desc);
      }
      else {
        if (percussion) {
          addPercNoteByDur(beginOffset, noteLengthBytes, key, vel, dur);
        }
        else {
          addNoteByDur(beginOffset, noteLengthBytes, key, vel, dur);
        }
        prevNoteKey = key;
      }

      if (slide) {
        beginPitchSlide(*slide);
      }
      else {
        setPitchBendRange(KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS);
      }

      prevNoteSlurred = (noteDurationRate == parentSeq->NOTE_DUR_RATE_MAX) && !percussion;
      addTime(len);

      break;
    }

    case EVENT_PERCUSSION_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion On", desc, Type::ChangeState);
      if (!percussion) {
        addProgramChange(beginOffset, curOffset - beginOffset, KonamiSnesInstrSet::DRUMKIT_PROGRAM, true);
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

      seqTuningCents = getTuningInSemitones(newTuning) * 100.0;
      applyEffectiveTuning(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_REST: {
      noteLength = readByte(curOffset++);

      const uint32_t restLengthBytes = curOffset - beginOffset;
      const auto slide = consumePitchSlide();

      addRest(beginOffset, restLengthBytes, noteLength);
      if (slide) {
        beginPitchSlide(*slide);
      }
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
        addTie(beginOffset, curOffset - beginOffset, dur, "Tie", desc);
        addTime(noteLength);
        prevNoteSlurred = (noteDurationRate == parentSeq->NOTE_DUR_RATE_MAX);
      }
      else {
        addTie(beginOffset, curOffset - beginOffset, noteLength, "Tie", desc);
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
        addGenericEvent(beginOffset, curOffset - beginOffset, "Per-Instrument Pan Off",
                        desc, Type::Pan);
      }
      else if (instrumentPanOn) {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Per-Instrument Pan On",
                        desc, Type::Pan);
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
      desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}",
                         vibratoDelay, vibratoRate, vibratoDepth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Vibrato);
      break;
    }

    case EVENT_RANDOM_PITCH: {
      uint8_t envRate = readByte(curOffset++);
      uint16_t envPitchMask = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Rate: {:d}  Pitch Mask: ${:04X}", envRate, envPitchMask);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Random Pitch", desc, Type::Modulation);
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

      desc = fmt::format("Times: {:d}  Volume Delta: {:d}  Pitch Delta: {:d}",
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
        applyEffectiveTuning(beginOffset, curOffset - beginOffset);

        assert(loopReturnAddr != 0);
      }
      else {
        loopCount = 0;
        loopVolumeDelta = 0;
        loopPitchDelta = 0;
        applyEffectiveTuning(beginOffset, curOffset - beginOffset);
      }
      break;
    }

      case EVENT_LOOP_START_2: {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start #2", desc, Type::RepeatStart);
        loopReturnAddr2 = curOffset;
        break;
      }

    case EVENT_LOOP_END_2: {
      uint8_t times = readByte(curOffset++);
      int8_t volumeDelta = readByte(curOffset++);
      int8_t pitchDelta = readByte(curOffset++);

      desc = fmt::format("Times: {:d}  Volume Delta: {:d}  Pitch Delta: {:d}",
                         times, volumeDelta, pitchDelta);
      if (times == 0) {
        bContinue = addLoopForever(beginOffset, curOffset - beginOffset, "Loop End #2");
      }
      else {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End #2", desc, Type::RepeatStart);
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
        applyEffectiveTuning(beginOffset, curOffset - beginOffset);

        assert(loopReturnAddr2 != 0);
      }
      else {
        loopCount2 = 0;
        loopVolumeDelta2 = 0;
        loopPitchDelta2 = 0;
        applyEffectiveTuning(beginOffset, curOffset - beginOffset);
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
      desc = fmt::format("BPM: {}  Fade Length: {}", parentSeq->getTempoInBPM(newTempo), fadeSpeed);
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
      volumeSlide.currentVolume = newVolume << 8;
      clearActiveVolumeSlide();
      uint8_t midiVolume = convertPercentAmpToStdMidiVal(newVolume / 255.0);
      addVol(beginOffset, curOffset - beginOffset, midiVolume);
      break;
    }

    case EVENT_VOLUME_SLIDE_V1:
    case EVENT_VOLUME_SLIDE_V2: {
      const auto slide = readVolumeSlide(eventType, beginOffset);
      curOffset += 2;
      beginVolumeSlide(slide);
      break;
    }

    case EVENT_PORTAMENTO: {
      uint8_t portamentoSpeed = readByte(curOffset++);
      desc = fmt::format("Portamento Speed: {:d}", portamentoSpeed);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento", desc, Type::Portamento);
      break;
    }

    case EVENT_PITCH_ENVELOPE_V1: {
      uint8_t pitchEnvDelay = readByte(curOffset++);
      uint8_t pitchEnvSpeed = readByte(curOffset++);
      uint8_t pitchEnvDepth = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Speed: {:d}  Depth: {:d}",
                         pitchEnvDelay, pitchEnvSpeed, -pitchEnvDepth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope", desc,
                      Type::PitchEnvelope);
      break;
    }

    case EVENT_PITCH_ENVELOPE_V2: {
      uint8_t pitchEnvDelay = readByte(curOffset++);
      uint8_t pitchEnvLength = readByte(curOffset++);
      uint8_t pitchEnvOffset = readByte(curOffset++);
      int16_t pitchDelta = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Delay: {:d}  Length: {:d}  Offset: {:d} semitones  Delta: {:.1f} semitones",
                         pitchEnvDelay, pitchEnvLength, -pitchEnvOffset, pitchDelta / 256.0);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope", desc,
                      Type::PitchEnvelope);
      break;
    }

    case EVENT_TUNING: {
      int8_t newTuning = (int8_t) readByte(curOffset++);
      seqTuningCents = getTuningInSemitones(newTuning) * 100.0;
      applyEffectiveTuning(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_PITCH_SLIDE_V1:
    case EVENT_PITCH_SLIDE_V3:
      addPitchSlideEvent(readPitchSlide(eventType, beginOffset));
      break;

    case EVENT_PITCH_SLIDE_V2: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc = fmt::format("Arg1: {:d}  Arg2: {:d}  Arg3: {:d}", arg1, arg2, arg3);

      if (arg2 != 0) {
        uint8_t arg4 = readByte(curOffset++);
        uint8_t arg5 = readByte(curOffset++);
        uint8_t arg6 = readByte(curOffset++);
        fmt::format_to(std::back_inserter(desc), "  Arg4: {:d}  Arg5: {:d}  Arg6: {:d}",
                       arg4, arg5, arg6);
      }

      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide", desc,
                      Type::PitchBendSlide);
      break;
    }

    case EVENT_ECHO: {
      uint8_t echoChannels = readByte(curOffset++);
      uint8_t echoVolumeL = readByte(curOffset++);
      uint8_t echoVolumeR = readByte(curOffset++);

      desc = fmt::format("EON: {:d}  EVOL(L): {:d}  EVOL(R): {:d}",
                         echoChannels, echoVolumeL, echoVolumeR);

      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo", desc, Type::Reverb);
      break;
    }

    case EVENT_ECHO_PARAM: {
      uint8_t echoDelay = readByte(curOffset++);
      uint8_t echoFeedback = readByte(curOffset++);
      uint8_t echoArg3 = readByte(curOffset++);

      desc = fmt::format("EDL: {:d}  EFB: {:d}  Arg3: {:d}", echoDelay, echoFeedback, echoArg3);

      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Param", desc, Type::Reverb);
      break;
    }

    case EVENT_LOOP_WITH_VOLTA_START: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop With Volta Start",
                      desc, Type::RepeatStart);

      voltaLoopStart = curOffset;
      voltaEndMeansPlayFromStart = false;
      voltaEndMeansPlayNextVolta = false;
      break;
    }

    case EVENT_LOOP_WITH_VOLTA_END: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop With Volta End",
                      desc, Type::RepeatStart);

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
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Fade", desc,
                      Type::Vibrato);
      break;
    }

    case EVENT_ADSR_GAIN: {
      uint8_t newADSR1 = readByte(curOffset++);
      uint8_t newADSR2 = readByte(curOffset++);
      uint8_t newGAINAmount = readByte(curOffset++);
      uint8_t newGAIN = convertGAINAmountToGAIN(newGAINAmount);

      desc = fmt::format("ADSR(1): ${:02X}  ADSR(2): ${:02X}  GAIN: ${:02X}",
                         newADSR1, newADSR2, newGAIN);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR(2)", desc, Type::Adsr);
      break;
    }

    case EVENT_CONDITIONAL_JUMP_V1: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      const uint16_t altDest = readShort(curOffset);
      desc = fmt::format("Destination: ${:04X}  Alternate Destination: ${:04X}", dest, altDest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Conditional Jump", desc,
                      Type::JumpConditional);

      // Contra III-style 0xFC chooses between two encoded destinations at runtime.
      // Follow the default path here so parsing stays in sync without depending on CPU state.
      curOffset = dest;
      bContinue = checkControlStateForInfiniteLoop(dest);
      break;
    }

    case EVENT_LINEAR_PITCH_ENVELOPE_V2: {
      const uint8_t deltaFraction = readByte(curOffset++);
      const uint8_t deltaInteger = readByte(curOffset++);
      const int16_t pitchDelta = static_cast<int16_t>(
          static_cast<uint16_t>(deltaFraction) | (static_cast<uint16_t>(deltaInteger) << 8));
      desc = fmt::format("Delta: {:.1f} semitones", pitchDelta / 256.0);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Linear Pitch Envelope", desc,
                      Type::PitchEnvelope);
      break;
    }

    case EVENT_GOTO: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      uint32_t length = curOffset - beginOffset;

      assert(dest >= offset());

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
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern Play", desc,
                      Type::RepeatStart);

      assert(dest >= offset());

      subReturnAddr = curOffset;
      inSubroutine = true;
      curOffset = dest;
      break;
    }

    case EVENT_END: {
      if (inSubroutine) {
        addGenericEvent(beginOffset, curOffset - beginOffset, "End Pattern", desc,
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
  //LogDebug(trace);

  return bContinue;
}
