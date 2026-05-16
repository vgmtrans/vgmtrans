/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoSnesSeq.h"
#include "automation/SeqTrackAutomation.h"
#include <cmath>

namespace {

static constexpr uint16_t AKAOSNES_DEFAULT_PITCH_BEND_RANGE_CENTS = 200;
static constexpr int32_t AKAOSNES_NOMINAL_DSP_PITCH = 0x1000;
static constexpr int32_t AKAOSNES_PITCH_FRACTION_SCALE = 0x100;

// For MIDI bend output, only target/base ratios matter, so normalize every new note to DSP pitch $1000.
int32_t akaoSnesBasePitch() {
  return AKAOSNES_NOMINAL_DSP_PITCH * AKAOSNES_PITCH_FRACTION_SCALE;
}

int32_t akaoSnesPitchForSemitoneOffset(int8_t semitones) {
  const double pitch =
      static_cast<double>(AKAOSNES_NOMINAL_DSP_PITCH) *
      std::pow(2.0, static_cast<double>(semitones) / 12.0);
  return static_cast<int32_t>(std::lround(pitch)) * AKAOSNES_PITCH_FRACTION_SCALE;
}

int32_t akaoSnesPitchSlideStep(AkaoSnesVersion version,
                               int32_t currentPitch,
                               int32_t targetPitch,
                               uint16_t steps) {
  const int32_t diff = targetPitch - currentPitch;
  if (version == AKAOSNES_V3) {
    const int32_t rawDiff = diff / AKAOSNES_PITCH_FRACTION_SCALE;
    int32_t rawStep = rawDiff / static_cast<int32_t>(steps);
    if (rawStep == 0 && rawDiff != 0) {
      rawStep = (rawDiff > 0) ? 1 : -1;
    }
    return rawStep * AKAOSNES_PITCH_FRACTION_SCALE;
  }

  return diff / static_cast<int32_t>(steps);
}

uint16_t akaoSnesV2PitchEnvelopeProgressStep(uint8_t length) {
  return length == 0 ? 0 : static_cast<uint16_t>(0xff00 / length);
}

double akaoSnesPitchCents(int32_t pitch, int32_t basePitch) {
  if (pitch <= 0 || basePitch <= 0) {
    return 0.0;
  }

  return 1200.0 * std::log2(static_cast<double>(pitch) / static_cast<double>(basePitch));
}

}  // namespace

void AkaoSnesTrack::resetPitchState() {
  pitchEnvelope = {};
  pendingPitchSlideSteps = 0;
  pendingPitchSlideSemitones = 0;
  pitchSlide.setPitchToCents(akaoSnesPitchCents);
  pitchSlide.reset(AKAOSNES_DEFAULT_PITCH_BEND_RANGE_CENTS);
}

void AkaoSnesTrack::updatePitchSlide() {
  advancePitchBendAutomation(pitchSlide);
}

void AkaoSnesTrack::updatePitchEnvelope() {
  const auto *parentSeq = static_cast<AkaoSnesSeq*>(this->parentSeq);
  if (parentSeq->version != AKAOSNES_V2 || !pitchEnvelope.active || !pitchSlide.baseValid()) {
    return;
  }

  if (pitchEnvelope.activeDelay > 1) {
    pitchEnvelope.activeDelay--;
    return;
  }
  if (pitchEnvelope.activeDelay == 1) {
    pitchEnvelope.activeDelay = 0;
  }

  pitchEnvelope.progress += pitchEnvelope.progressStep;

  int32_t currentOffset;
  if (pitchEnvelope.progress > 0xffff) {
    currentOffset = pitchEnvelope.targetOffset;
    pitchEnvelope.progress = 0x10000;
    pitchEnvelope.active = false;
  }
  else {
    const uint8_t progressHigh = static_cast<uint8_t>(pitchEnvelope.progress >> 8);
    const int32_t targetMagnitude =
        pitchEnvelope.targetOffset < 0 ? -pitchEnvelope.targetOffset : pitchEnvelope.targetOffset;
    int32_t currentMagnitude =
        (targetMagnitude / AKAOSNES_PITCH_FRACTION_SCALE) * progressHigh / 256;
    currentMagnitude *= AKAOSNES_PITCH_FRACTION_SCALE;
    currentOffset = pitchEnvelope.targetOffset < 0 ? -currentMagnitude : currentMagnitude;
  }

  pitchSlide.setCurrentPitch(pitchSlide.basePitch() + currentOffset);
  applyPitchBendAutomation(pitchSlide);
}

void AkaoSnesTrack::resetPitchBendForNewNote() {
  const auto *parentSeq = static_cast<AkaoSnesSeq*>(this->parentSeq);
  pitchSlide.clearMotion();
  pitchSlide.invalidateBase();

  if (!pitchSlide.atRest(AKAOSNES_DEFAULT_PITCH_BEND_RANGE_CENTS)) {
    if (parentSeq->version == AKAOSNES_V2 && pitchEnvelope.enabled) {
      setPitchBendAutomationBend(pitchSlide, 0);
      return;
    }
    resetPitchBendAutomation(pitchSlide, AKAOSNES_DEFAULT_PITCH_BEND_RANGE_CENTS);
  }
}

void AkaoSnesTrack::beginNotePitch(uint8_t, bool validForPitchBend) {
  resetPitchBendForNewNote();
  if (validForPitchBend) {
    pitchSlide.beginNote(akaoSnesBasePitch());
    beginPitchEnvelopeForNote();
  }
}

void AkaoSnesTrack::setPitchEnvelope(int8_t semitones, uint8_t delay, uint8_t length) {
  if (semitones == 0 || length == 0) {
    clearPitchEnvelope();
    return;
  }

  pitchEnvelope.enabled = true;
  pitchEnvelope.semitones = semitones;
  pitchEnvelope.delay = delay;
  pitchEnvelope.length = length;
  pitchEnvelope.progressStep = akaoSnesV2PitchEnvelopeProgressStep(length);
}

void AkaoSnesTrack::clearPitchEnvelope() {
  pitchEnvelope.enabled = false;
  pitchEnvelope.active = false;
  pitchEnvelope.semitones = 0;
  pitchEnvelope.delay = 0;
  pitchEnvelope.length = 0;
  pitchEnvelope.progressStep = 0;
  pitchEnvelope.activeDelay = 0;
  pitchEnvelope.progress = 0;
  pitchEnvelope.targetOffset = 0;
}

void AkaoSnesTrack::beginPitchEnvelopeForNote() {
  const auto *parentSeq = static_cast<AkaoSnesSeq*>(this->parentSeq);
  if (parentSeq->version != AKAOSNES_V2 || !pitchEnvelope.enabled || !pitchSlide.baseValid()) {
    return;
  }

  const int32_t targetPitch = akaoSnesPitchForSemitoneOffset(pitchEnvelope.semitones);
  const int32_t rawDiff =
      (targetPitch - pitchSlide.basePitch()) / AKAOSNES_PITCH_FRACTION_SCALE;
  const int32_t rawMagnitude = rawDiff < 0 ? -rawDiff : rawDiff;
  const int32_t signedMagnitude =
      pitchEnvelope.semitones < 0 ? -rawMagnitude : rawMagnitude;
  pitchEnvelope.targetOffset = signedMagnitude * AKAOSNES_PITCH_FRACTION_SCALE;
  pitchEnvelope.activeDelay = pitchEnvelope.delay;
  pitchEnvelope.progress = 0;
  pitchEnvelope.active = pitchEnvelope.targetOffset != 0 && pitchEnvelope.progressStep != 0;
  pitchSlide.setCurrentPitch(pitchSlide.basePitch());

  if (pitchEnvelope.active) {
    setPitchBendAutomationRange(
        pitchSlide,
        pitchSlide.rangeCentsForSlide(pitchSlide.basePitch(),
                                      pitchSlide.basePitch() + pitchEnvelope.targetOffset,
                                      AKAOSNES_DEFAULT_PITCH_BEND_RANGE_CENTS));
  }
}

void AkaoSnesTrack::setPendingPitchSlide(uint16_t steps, int8_t semitones) {
  pendingPitchSlideSteps = steps;
  pendingPitchSlideSemitones = semitones;
  if (pendingPitchSlideSemitones == 0) {
    clearPendingPitchSlide();
  }
}

void AkaoSnesTrack::clearPendingPitchSlide() {
  pendingPitchSlideSteps = 0;
  pendingPitchSlideSemitones = 0;
}

void AkaoSnesTrack::beginPendingPitchSlide() {
  const auto *parentSeq = static_cast<AkaoSnesSeq*>(this->parentSeq);
  if (pendingPitchSlideSteps == 0 || pendingPitchSlideSemitones == 0) {
    clearPendingPitchSlide();
    return;
  }

  const uint16_t steps = pendingPitchSlideSteps;
  const int8_t semitones = pendingPitchSlideSemitones;
  clearPendingPitchSlide();

  if (!pitchSlide.baseValid()) {
    return;
  }

  const int32_t currentPitch = pitchSlide.currentPitch();
  const int32_t targetPitch = akaoSnesPitchForSemitoneOffset(semitones);
  const int32_t step =
      akaoSnesPitchSlideStep(parentSeq->version, currentPitch, targetPitch, steps);

  beginPitchBendAutomation(
      pitchSlide,
      pitchSlide.motionToTargetWithStepNoSnap(targetPitch, step, steps),
      pitchSlide.rangeCentsForSlide(currentPitch, targetPitch, AKAOSNES_DEFAULT_PITCH_BEND_RANGE_CENTS),
      AKAOSNES_DEFAULT_PITCH_BEND_RANGE_CENTS,
      true);

  // Apply the first update on the same sequencer tick that consumes the setup command.
  updatePitchSlide();
}
