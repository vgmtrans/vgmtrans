/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiSnesSeq.h"
#include "KonamiSnesInstr.h"
#include "KonamiSnesVibrato.h"
#include "ScaleConversion.h"
#include "automation/SeqTrackAutomation.h"
#include "spdlog/fmt/fmt.h"
#include <algorithm>
#include <cmath>

DECLARE_FORMAT(KonamiSnes);

namespace {
constexpr uint32_t MAX_TRACKS = 8;
constexpr uint16_t SEQ_PPQN = 48;
constexpr uint16_t KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS = 200;
constexpr uint8_t KONAMI_SNES_DEFAULT_TEMPO = 0xff;

using KonamiControllerMotion = SeqFixedPointMotion<int32_t>;
using KonamiPitchMotion = SeqMotionPlan<double>;

constexpr uint8_t noteDurationRateMax(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 ? 100 : 127;
}

constexpr uint8_t timerFrequency(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 ? 0x20 : 0x40;
}

void fillEventRange(std::map<uint8_t, KonamiSnesSeqEventType>& eventMap,
                    uint8_t firstStatusByte,
                    uint8_t lastStatusByte,
                    KonamiSnesSeqEventType eventType) {
  for (uint8_t statusByte = firstStatusByte; statusByte <= lastStatusByte; statusByte++) {
    eventMap[statusByte] = eventType;
  }
}

uint8_t convertVibratoDepthToMidi(KonamiSnesVersion version,
                                  uint8_t targetDepth,
                                  uint16_t currentDepth,
                                  uint8_t maxDepth) {
  if (targetDepth == 0 || currentDepth == 0) {
    return 0;
  }

  const uint8_t clampedMaxDepth = std::max(maxDepth, konami_snes::kMinVibratoMaxDepth);
  const double depthCents = konami_snes::vibrato::currentDepthCents(version, targetDepth, currentDepth);
  const double maxDepthCents = konami_snes::vibrato::maxDepthCents(version, clampedMaxDepth);
  const int midiValue = static_cast<int>(std::lround(128.0 * depthCents / maxDepthCents));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

uint8_t convertVibratoRateToMidi(KonamiSnesVersion version,
                                 uint8_t rate,
                                 uint8_t tempo,
                                 uint16_t maxRateFactor) {
  const uint16_t effectiveRateFactor = konami_snes::vibrato::rateFactor(version, rate, tempo);
  if (effectiveRateFactor == 0) {
    return 0;
  }

  const uint16_t clampedMaxRateFactor = std::max(maxRateFactor, konami_snes::vibrato::minMaxRateFactor(version));
  const double baseHz = konami_snes::vibrato::baseHz(version);
  return midiValueForHertzInRange(baseHz * effectiveRateFactor, baseHz, baseHz * clampedMaxRateFactor);
}

uint8_t convertVibratoDelayToMidi(KonamiSnesVersion version, uint8_t delay, uint8_t tempo) {
  const double delaySeconds = konami_snes::vibrato::delaySeconds(version, delay, tempo);
  return midiValueForSecondsInRange(delaySeconds,
                                    konami_snes::vibrato::minDelaySeconds(version),
                                    konami_snes::vibrato::maxDelaySeconds(version));
}
}

//  **********
//  KonamiSnesSeq
//  **********
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

// volume curve table
const uint8_t KonamiSnesSeq::VOL_TABLE[] = {
  0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02,
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04,
  0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05,
  0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08,
  0x09, 0x09, 0x0a, 0x0a, 0x0a, 0x0a, 0x0b, 0x0b,
  0x0c, 0x0c, 0x0d, 0x0d, 0x0e, 0x0f, 0x10, 0x10,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1b, 0x1c, 0x1d, 0x1e, 0x20, 0x22,
  0x23, 0x24, 0x26, 0x28, 0x2a, 0x2c, 0x2d, 0x2f,
  0x31, 0x33, 0x35, 0x38, 0x3a, 0x3d, 0x40, 0x43,
  0x46, 0x49, 0x4c, 0x4f, 0x52, 0x56, 0x5a, 0x5e,
  0x62, 0x66, 0x6b, 0x6f, 0x73, 0x77, 0x7b, 0x7f
};

KonamiSnesSeq::KonamiSnesSeq(RawFile *file, KonamiSnesVersion ver, uint32_t seqdataOffset, std::string newName)
    : VGMSeq(KonamiSnesFormat::name, file, seqdataOffset, 0, newName),
      maxVibratoDepth(konami_snes::kMinVibratoMaxDepth),
      maxVibratoRateFactor(konami_snes::vibrato::minMaxRateFactor(ver)),
      version(ver) {
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

  if (readMode != READMODE_CONVERT_TO_MIDI) {
    maxVibratoDepth = konami_snes::kMinVibratoMaxDepth;
    maxVibratoRateFactor = konami_snes::vibrato::minMaxRateFactor(version);
  }

  // The driver starts from tempo/speed FF unless the sequence overrides it.
  tempo = KONAMI_SNES_DEFAULT_TEMPO;
  tempoFade.setCurrentRaw(tempo);
  tempoFadeLastUpdatedTime = static_cast<uint32_t>(-1);
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
  NOTE_DUR_RATE_MAX = noteDurationRateMax(version);

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
    fillEventRange(EventMap, 0x65, 0x7f, EVENT_UNKNOWN0);
  }
  else {
    EventMap[0x62] = EVENT_GAIN;
    fillEventRange(EventMap, 0x63, 0x7f, EVENT_UNKNOWN0);
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
  EventMap[0xeb] = EVENT_TEMPO_FADE_V1;
  EventMap[0xec] = EVENT_TRANSPABS;
  EventMap[0xed] = EVENT_ADSR1;
  EventMap[0xee] = EVENT_VOLUME;
  EventMap[0xef] = EVENT_VOLUME_FADE_V1;
  EventMap[0xf0] = EVENT_PORTAMENTO;
  EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V2;
  EventMap[0xf2] = EVENT_TUNING;
  EventMap[0xf3] = EVENT_PITCH_SLIDE_V3;
  EventMap[0xf4] = EVENT_ECHO;
  EventMap[0xf5] = EVENT_ECHO_PARAM;
  EventMap[0xf6] = EVENT_LOOP_WITH_VOLTA_START;
  EventMap[0xf7] = EVENT_LOOP_WITH_VOLTA_END;
  EventMap[0xf8] = EVENT_PAN_FADE_V1;
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
    case KONAMISNES_V6:
      fillEventRange(EventMap, 0x70, 0x7f, EVENT_INSTANT_TUNING);
      EventMap[0xed] = EVENT_ADSR1;
      EventMap[0xeb] = EVENT_TEMPO_FADE_V2;
      EventMap[0xef] = EVENT_VOLUME_FADE_V2;
      EventMap[0xf1] = EVENT_PITCH_ENVELOPE_V2;
      EventMap[0xf3] = EVENT_PITCH_SLIDE_V3;
      EventMap[0xf8] = EVENT_PAN_FADE_V2;
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
  if (tempo == 0) {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }

  return 60000000.0 / (SEQ_PPQN * (125 * timerFrequency(version))) * (tempo / 256.0);
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

  panFade.setCurrentRaw(defaultPanValue());
  volumeFade.setCurrentRaw(0xff);
  pitchSlide.reset(KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS);
  vibrato.reset();
}

KonamiSnesSeq& KonamiSnesTrack::seq() {
  return *static_cast<KonamiSnesSeq*>(parentSeq);
}

const KonamiSnesSeq& KonamiSnesTrack::seq() const {
  return *static_cast<const KonamiSnesSeq*>(parentSeq);
}

double KonamiSnesTrack::getTuningInSemitones(int8_t tuning) {
  return tuning * 4 / 256.0;
}

void KonamiSnesTrack::syncVibratoRateAndDelay() {
  if (!konami_snes::vibrato::isActive(seq().version, vibrato.rate(), vibrato.depth())) {
    return;
  }

  auto &parentSeq = seq();
  if (readMode != READMODE_CONVERT_TO_MIDI) {
    parentSeq.maxVibratoRateFactor = std::max(parentSeq.maxVibratoRateFactor,
                                              konami_snes::vibrato::rateFactor(parentSeq.version, vibrato.rate(), parentSeq.tempo));
  }

  addChannelPressureNoItem(convertVibratoRateToMidi(parentSeq.version,
                                                    vibrato.rate(),
                                                    parentSeq.tempo,
                                                    parentSeq.maxVibratoRateFactor));
  addControllerEventNoItem(konami_snes::kVibratoDelayController,
                           convertVibratoDelayToMidi(parentSeq.version, vibrato.delay(), parentSeq.tempo));
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
  auto &parentSeq = seq();
  auto &tempoFade = parentSeq.tempoFade;
  if (parentSeq.tempoFadeLastUpdatedTime != getTime()) {
    parentSeq.tempoFadeLastUpdatedTime = getTime();
    tempoFade.tickRaw([this](int32_t) { applyCurrentTempo(); });
  }

  panFade.tickRaw([this](int32_t) { applyCurrentPan(); });
  volumeFade.tickRaw([this](int32_t) { applyCurrentVolume(); });

  advanceSynthLfoFadeToModulation(vibrato, 8, [this](int32_t depth) {
    return convertVibratoDepthToMidi(seq().version,
                                     vibrato.depth(),
                                     static_cast<uint16_t>(depth),
                                     seq().maxVibratoDepth);
  });

  if (pitchSlide.motionActive()) {
    advancePitchBendAutomation(pitchSlide);
  }
}

std::optional<KonamiSnesTrack::PitchSlide> KonamiSnesTrack::consumePitchSlide() {
  const auto &parentSeq = seq();
  const auto statusByte = readByte(curOffset);
  auto nextEvent = parentSeq.EventMap.find(statusByte);
  if (nextEvent == parentSeq.EventMap.end()) {
    return std::nullopt;
  }

  switch (nextEvent->second) {
    case EVENT_PITCH_SLIDE_V1:
    case EVENT_PITCH_SLIDE_V2:
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
      if (slide.length != 0 && pitchSlide.baseValid()) {
        slide.delta = static_cast<int16_t>(((slide.targetSemitones - pitchSlide.currentPitch()) * 256.0)
                                           / slide.length);
        slide.deltaSemitones = slide.delta / 256.0;
      }
      break;

    case EVENT_PITCH_SLIDE_V2:
      slide.eventLength = 4;
      slide.targetSemitones = noteSemitones(slide.targetNote, false);
      if (slide.length != 0) {
        slide.eventLength = 7;
        curOffset += 1;
        slide.delta = static_cast<int16_t>(readShort(curOffset));
        slide.deltaSemitones = slide.delta / 256.0;
        curOffset += 2;
      }
      break;

    case EVENT_PITCH_SLIDE_V3:
      slide.eventLength = 6;
      slide.targetSemitones = noteSemitones(slide.targetNote, false);
      slide.delta = static_cast<int16_t>(readShort(curOffset));
      slide.deltaSemitones = (slide.delta / 256.0)
                             * (256.0 / std::max<uint8_t>(seq().tempo, 1));
      curOffset += 2;
      break;

    default:
      assert(false);
      break;
  }
  return slide;
}

void KonamiSnesTrack::clearActivePitchSlide() {
  pitchSlide.clearMotion();
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
  setPitchBendAutomationBend(pitchSlide, 0);

  if (percussion) {
    pitchSlide.invalidateBase();
    return;
  }

  pitchSlide.beginNote(noteSemitones(key, true));
}

uint16_t KonamiSnesTrack::pitchSlideRangeCents(const PitchSlide& slide) const {
  double maxSlideSemitones = std::abs(slide.targetSemitones - pitchSlide.basePitch());
  if (slide.length > 1) {
    maxSlideSemitones = std::max(maxSlideSemitones, std::abs((slide.length - 1) * slide.deltaSemitones));
  }

  return pitchSlide.rangeCentsForPitchSpan(std::ceil(maxSlideSemitones), KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS);
}

uint8_t KonamiSnesTrack::getNoteDuration(uint8_t length, uint8_t durationRate) const {
  const auto &parentSeq = seq();
  if (durationRate == parentSeq.NOTE_DUR_RATE_MAX) {
    return length;
  }

  uint8_t duration = 0;
  if (parentSeq.version == KONAMISNES_V1) {
    duration = (length * durationRate) / 100;
  }
  else {
    duration = (length * (durationRate << 1)) >> 8;
  }

  return std::max<uint8_t>(duration, 1);
}

void KonamiSnesTrack::beginPitchSlide(const PitchSlide& slide) {
  addPitchSlideEvent(slide);

  if (!pitchSlide.baseValid() || slide.length == 0) {
    clearActivePitchSlide();
    setPitchBendAutomationRange(pitchSlide, KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS);
    return;
  }

  beginPitchBendAutomation(
      pitchSlide,
      KonamiPitchMotion::targetOverTicksWithStep(slide.targetSemitones,
                                                 slide.deltaSemitones,
                                                 slide.length,
                                                 slide.delay),
      pitchSlideRangeCents(slide),
      KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS);
}

KonamiSnesTrack::ControllerFade KonamiSnesTrack::readVolumeFade(KonamiSnesSeqEventType eventType, uint32_t offset) const {
  ControllerFade fade {offset, {}};

  switch (eventType) {
    case EVENT_VOLUME_FADE_V1:
      fade.motion = KonamiControllerMotion::toRawTarget(readByte(curOffset + 1), readByte(curOffset));
      break;

    case EVENT_VOLUME_FADE_V2:
      fade.motion = KonamiControllerMotion::toRawTargetByFixedStep(
          readByte(curOffset),
          static_cast<int16_t>(static_cast<int8_t>(readByte(curOffset + 1)) << 4));
      break;

    default:
      assert(false);
      break;
  }

  return fade;
}

void KonamiSnesTrack::applyCurrentVolume() {
  const uint8_t rawVolume = static_cast<uint8_t>(std::clamp(volumeFade.currentRaw(), 0, 0xff));
  const uint8_t midiVolume = convertPercentAmpToStdMidiVal(rawVolume / 255.0);
  if (midiVolume != vol) {
    addVolNoItem(midiVolume);
  }
}

uint8_t KonamiSnesTrack::defaultPanValue() const {
  return seq().version <= KONAMISNES_V2 ? 10 : 20;
}

uint8_t KonamiSnesTrack::clampPanValue(uint8_t pan) const {
  return std::min(pan, seq().version <= KONAMISNES_V2 ? uint8_t {20} : uint8_t {40});
}

uint8_t KonamiSnesTrack::convertPanValueToMidiPan(uint8_t pan) const {
  const auto version = seq().version;
  uint8_t volumeLeft;
  uint8_t volumeRight;
  switch (version) {
    case KONAMISNES_V1:
      pan = std::min(pan, static_cast<uint8_t>(20));
      volumeLeft = KonamiSnesSeq::PAN_VOLUME_LEFT_V1[pan];
      volumeRight = KonamiSnesSeq::PAN_VOLUME_RIGHT_V1[pan];
      break;

    case KONAMISNES_V2:
      pan = std::min(pan, static_cast<uint8_t>(20));
      volumeLeft = KonamiSnesSeq::PAN_VOLUME_LEFT_V2[pan];
      volumeRight = KonamiSnesSeq::PAN_VOLUME_RIGHT_V2[pan];
      break;

    default:
      pan = std::min(pan, static_cast<uint8_t>(40));
      volumeLeft = KonamiSnesSeq::PAN_TABLE[40 - pan];
      volumeRight = KonamiSnesSeq::PAN_TABLE[pan];
      break;
  }

  return convertLinearPercentPanValToStdMidiVal(static_cast<double>(volumeRight) / (volumeLeft + volumeRight));
}

KonamiSnesTrack::ControllerFade KonamiSnesTrack::readPanFade(KonamiSnesSeqEventType eventType, uint32_t offset) const {
  ControllerFade fade {offset, {}};

  switch (eventType) {
    case EVENT_PAN_FADE_V1:
      fade.motion = KonamiControllerMotion::toRawTarget(clampPanValue(readByte(curOffset + 1)),
                                                        readByte(curOffset));
      break;

    case EVENT_PAN_FADE_V2:
      fade.motion = KonamiControllerMotion::toRawTargetByFixedStep(
          clampPanValue(readByte(curOffset)),
          static_cast<int16_t>(static_cast<int8_t>(readByte(curOffset + 1)) << 4));
      break;

    default:
      assert(false);
      break;
  }

  return fade;
}

void KonamiSnesTrack::applyCurrentPan() {
  addPanNoItem(convertPanValueToMidiPan(clampPanValue(panFade.currentRaw())));
}

KonamiSnesTrack::ControllerFade KonamiSnesTrack::readTempoFade(KonamiSnesSeqEventType eventType, uint32_t offset) const {
  ControllerFade fade {offset, {}};

  switch (eventType) {
    case EVENT_TEMPO_FADE_V1:
      fade.motion = KonamiControllerMotion::toRawTarget(readByte(curOffset + 1), readByte(curOffset));
      break;

    case EVENT_TEMPO_FADE_V2:
      fade.motion = KonamiControllerMotion::toRawTargetByFixedStep(
          readByte(curOffset),
          static_cast<int16_t>(static_cast<int8_t>(readByte(curOffset + 1)) << 4));
      break;

    default:
      assert(false);
      break;
  }

  return fade;
}

void KonamiSnesTrack::applyCurrentTempo() {
  auto &parentSeq = seq();
  const auto newTempo = static_cast<uint8_t>(std::clamp(parentSeq.tempoFade.currentRaw(), 0, 0xff));
  if (newTempo != parentSeq.tempo) {
    parentSeq.tempo = newTempo;
    addTempoBPMNoItem(parentSeq.getTempoInBPM(newTempo));
    if (konami_snes::usesLegacyVibrato(parentSeq.version)) {
      for (SeqTrack *track : parentSeq.aTracks) {
        static_cast<KonamiSnesTrack*>(track)->syncVibratoRateAndDelay();
      }
    }
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

void KonamiSnesTrack::addUnknownEvent(uint32_t beginOffset, uint8_t statusByte, uint8_t argCount) {
  std::string desc = fmt::format("Event: 0x{:02X}", statusByte);
  for (uint8_t argIndex = 0; argIndex < argCount; argIndex++) {
    desc += fmt::format("  Arg{}: {:d}", argIndex + 1, readByte(curOffset++));
  }

  addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
}

void KonamiSnesTrack::resetPanAfterProgramChange() {
  panFade.setCurrentRaw(defaultPanValue());
  addPanNoItem(64); // TODO: apply true pan from instrument table
}

bool KonamiSnesTrack::readEvent() {
  auto &parentSeq = seq();
  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  KonamiSnesSeqEventType eventType = (KonamiSnesSeqEventType) 0;
  auto pEventType = parentSeq.EventMap.find(statusByte);
  if (pEventType != parentSeq.EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      addUnknownEvent(beginOffset, statusByte, 0);
      break;

    case EVENT_UNKNOWN1:
      addUnknownEvent(beginOffset, statusByte, 1);
      break;

    case EVENT_UNKNOWN2:
      addUnknownEvent(beginOffset, statusByte, 2);
      break;

    case EVENT_UNKNOWN3:
      addUnknownEvent(beginOffset, statusByte, 3);
      break;

    case EVENT_UNKNOWN4:
      addUnknownEvent(beginOffset, statusByte, 4);
      break;

    case EVENT_UNKNOWN5:
      addUnknownEvent(beginOffset, statusByte, 5);
      break;

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
        noteDurationRate = std::min(vel, parentSeq.NOTE_DUR_RATE_MAX);
        vel = readByte(curOffset++);
      }
      vel &= 0x7f;

      if (vel == 0) {
        vel = 1; // TODO: verification
      }

      vel = static_cast<uint8_t>(std::clamp<int>(vel + getLoopVolumeDelta(), 1, 127));
      if (seq().version != KONAMISNES_V1) {
        vel = KonamiSnesSeq::VOL_TABLE[vel];
      }
      vel = convertPercentAmpToStdMidiVal(vel / 127.0);
      applyEffectiveTuning(beginOffset, curOffset - beginOffset);

      const uint8_t dur = getNoteDuration(len, noteDurationRate);

      const uint32_t noteLengthBytes = curOffset - beginOffset;
      resetPitchForNote(key);
      const auto slide = consumePitchSlide();
      const bool isTiedNote = prevNoteSlurred && key == prevNoteKey;

      if (!isTiedNote && vibrato.hasReusableFade() &&
          konami_snes::vibrato::isActive(seq().version, vibrato.rate(), vibrato.depth())) {
        vibrato.beginReusableFadeToConfiguredDepth(8);
        setSynthLfoModulationDepth(vibrato, 0);
      }

      if (isTiedNote) {
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
        setPitchBendAutomationRange(pitchSlide, KONAMI_SNES_STD_PITCH_BEND_RANGE_CENTS);
      }

      prevNoteSlurred = (noteDurationRate == parentSeq.NOTE_DUR_RATE_MAX) && !percussion;
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
      noteDurationRate = std::min(noteDurationRate, parentSeq.NOTE_DUR_RATE_MAX);
      if (prevNoteSlurred) {
        const uint8_t dur = getNoteDuration(noteLength, noteDurationRate);

        makePrevDurNoteEnd(getTime() + dur);
        addTie(beginOffset, curOffset - beginOffset, dur, "Tie", desc);
        addTime(noteLength);
        prevNoteSlurred = (noteDurationRate == parentSeq.NOTE_DUR_RATE_MAX);
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
      resetPanAfterProgramChange();
      break;
    }

    case EVENT_PROGCHANGEVOL: {
      uint8_t newVolume = readByte(curOffset++);
      uint8_t newProg = readByte(curOffset++);

      instrument = newProg;
      addProgramChange(beginOffset, curOffset - beginOffset, newProg, true);

      uint8_t midiVolume = convertPercentAmpToStdMidiVal(newVolume / 255.0);
      addVolNoItem(midiVolume);
      resetPanAfterProgramChange();
      break;
    }

    case EVENT_PAN: {
      uint8_t newPan = readByte(curOffset++);
      panFade.clearMotion();

      bool instrumentPanOff;
      bool instrumentPanOn;
      switch (parentSeq.version) {
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
        newPan = clampPanValue(newPan);
        panFade.setCurrentRaw(newPan);
        const uint8_t midiPan = convertPanValueToMidiPan(newPan);
        // TODO: apply volume scale
        addPan(beginOffset, curOffset - beginOffset, midiPan);
      }
      break;
    }

    case EVENT_VIBRATO: {
      uint8_t vibratoArg1 = readByte(curOffset++);
      uint8_t newVibratoRate = readByte(curOffset++);
      uint8_t newVibratoDepth = readByte(curOffset++);
      const uint8_t builtInFadeLength = konami_snes::vibrato::inlineFadeLength(parentSeq.version, vibratoArg1);
      const uint8_t newVibratoDelay = konami_snes::vibrato::delayFromArg1(parentSeq.version, vibratoArg1);
      desc = (builtInFadeLength != 0)
          ? fmt::format("Fade Length: {:d}  Rate: {:d}  Depth: {:d}",
                        builtInFadeLength, newVibratoRate, newVibratoDepth)
          : fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}",
                        newVibratoDelay, newVibratoRate, newVibratoDepth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Vibrato);

      uint8_t immediateFadeLength = builtInFadeLength;
      if (readByte(curOffset) == 0xf9) {
        immediateFadeLength = readByte(curOffset + 1);
      }

      vibrato.configure(newVibratoDelay, newVibratoRate, newVibratoDepth);
      if (builtInFadeLength != 0) {
        vibrato.setReusableFadeToConfiguredDepth(builtInFadeLength, 8);
      }

      const bool active = konami_snes::vibrato::isActive(parentSeq.version, vibrato.rate(), vibrato.depth());
      const bool deferDepthForFade = active && immediateFadeLength != 0;
      if (readMode != READMODE_CONVERT_TO_MIDI && active) {
        parentSeq.maxVibratoDepth = std::max(parentSeq.maxVibratoDepth, vibrato.depth());
      }

      const uint8_t midiDepth = deferDepthForFade
          ? 0
          : (active ? convertVibratoDepthToMidi(parentSeq.version,
                                                vibrato.depth(),
                                                static_cast<uint16_t>(vibrato.depth()) << 8,
                                                parentSeq.maxVibratoDepth)
                    : 0);
      vibrato.setCurrentDepthPreservingMotion(deferDepthForFade ? 0 : vibrato.configuredDepth(8));
      setSynthLfoModulationDepth(vibrato, midiDepth, true);
      if (active) {
        syncVibratoRateAndDelay();
      }
      else {
        addChannelPressureNoItem(0);
        addControllerEventNoItem(konami_snes::kVibratoDelayController, 0);
      }
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
      parentSeq.tempoFade.setCurrentRaw(newTempo);
      parentSeq.tempo = newTempo;
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq.getTempoInBPM(newTempo));
      if (konami_snes::usesLegacyVibrato(parentSeq.version)) {
        for (SeqTrack *track : parentSeq.aTracks) {
          static_cast<KonamiSnesTrack*>(track)->syncVibratoRateAndDelay();
        }
      }
      break;
    }

    case EVENT_TEMPO_FADE_V1:
    case EVENT_TEMPO_FADE_V2: {
      const auto fade = readTempoFade(eventType, beginOffset);
      curOffset += 2;
      const auto bpm = parentSeq.getTempoInBPM(static_cast<uint8_t>(fade.motion.targetRaw));
      desc = fade.motion.usesTicks()
          ? fmt::format("Length: {:d}  Target BPM: {}", fade.motion.ticks, bpm)
          : fmt::format("Target BPM: {}  Speed: {:.2f}", bpm, fade.motion.stepFixed / 256.0);
      addGenericEvent(fade.offset, 3, "Tempo Fade", desc, Type::Tempo);
      parentSeq.tempoFade.begin(fade.motion, [this](int32_t) { applyCurrentTempo(); });
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
      volumeFade.setCurrentRaw(newVolume);
      uint8_t midiVolume = convertPercentAmpToStdMidiVal(newVolume / 255.0);
      addVol(beginOffset, curOffset - beginOffset, midiVolume);
      break;
    }

    case EVENT_VOLUME_FADE_V1:
    case EVENT_VOLUME_FADE_V2: {
      const auto fade = readVolumeFade(eventType, beginOffset);
      curOffset += 2;
      desc = fade.motion.usesTicks()
          ? fmt::format("Length: {:d}  Target Volume: {:d}", fade.motion.ticks, fade.motion.targetRaw)
          : fmt::format("Target Volume: {:d}  Speed: {:.2f}",
                        fade.motion.targetRaw,
                        fade.motion.stepFixed / 256.0);
      addGenericEvent(fade.offset, 3, "Volume Fade", desc, Type::VolumeSlide);
      volumeFade.begin(fade.motion, [this](int32_t) { applyCurrentVolume(); });
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
    case EVENT_PITCH_SLIDE_V2:
    case EVENT_PITCH_SLIDE_V3:
      addPitchSlideEvent(readPitchSlide(eventType, beginOffset));
      break;

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

    case EVENT_PAN_FADE_V1:
    case EVENT_PAN_FADE_V2: {
      const auto fade = readPanFade(eventType, beginOffset);
      curOffset += 2;
      desc = fade.motion.usesTicks()
          ? fmt::format("Length: {:d}  Target Pan: {:d}", fade.motion.ticks, fade.motion.targetRaw)
          : fmt::format("Target Pan: {:d}  Speed: {:.2f}",
                        fade.motion.targetRaw,
                        fade.motion.stepFixed / 256.0);
      addGenericEvent(fade.offset, 3, "Pan Fade", desc, Type::PanSlide);
      panFade.begin(fade.motion, [this](int32_t) { applyCurrentPan(); });
      break;
    }

    case EVENT_VIBRATO_FADE: {
      uint8_t fadeSpeed = readByte(curOffset++);
      desc = fmt::format("Fade Length: {:d}", fadeSpeed);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Fade", desc, Type::Vibrato);
      vibrato.setReusableFadeToConfiguredDepth(fadeSpeed, 8);
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
        addGenericEvent(beginOffset, curOffset - beginOffset, "End Pattern", desc, Type::RepeatEnd);

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
