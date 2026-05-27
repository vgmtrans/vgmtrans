/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoSnesSeq.h"
#include "automation/SeqTrackAutomation.h"
#include <algorithm>
#include <cmath>

namespace {

static constexpr uint16_t kDefaultPitchBendRangeCents = 200;
static constexpr int32_t kNominalDspPitch = 0x1000;
static constexpr int32_t kPitchFractionScale = 0x100;

/*
 * AkaoSnes has two different families of pitch automation:
 *
 * V1 and V2 use persistent pitch envelopes. The setup command stays
 * active until the matching off command, and every later normal note starts a
 * fresh ramp from that note's base pitch toward note + signed semitone offset.
 * Ties do not restart the envelope.
 *
 * V3 and V4 use one-shot pitch slides. The command only arms the next
 * note or tie; rests do not consume it. The slide target is also relative to
 * the note/tie being initialized, not to the previous note.
 *
 * MIDI pitch bend only needs ratios between base and current pitch, so we
 * normalize each started note to DSP pitch $1000 and express all automation in
 * that source-space pitch, with an 8-bit fractional scale where needed.
 */

int16_t akaoSnesCorrectedNote(uint8_t note, int8_t transpose) {
  return static_cast<int16_t>(note) + static_cast<int16_t>(transpose) - 10;
}

int32_t akaoSnesPitchForSemitoneOffset(int semitones) {
  const double pitch =
      static_cast<double>(kNominalDspPitch) * std::pow(2.0, static_cast<double>(semitones) / 12.0);
  return static_cast<int32_t>(std::lround(pitch)) * kPitchFractionScale;
}

int32_t akaoSnesPitchSlideStep(AkaoSnesVersion version,
                               int32_t currentPitch,
                               int32_t targetPitch,
                               uint16_t steps) {
  const int32_t diff = targetPitch - currentPitch;

  // V3 steps a signed 16-bit DSP pitch word directly and forces a nonzero
  // step when the target differs. This can overshoot for tiny long slides.
  if (version == AKAOSNES_V3) {
    const int32_t rawDiff = diff / kPitchFractionScale;
    int32_t rawStep = rawDiff / static_cast<int32_t>(steps);
    if (rawStep == 0 && rawDiff != 0) {
      rawStep = (rawDiff > 0) ? 1 : -1;
    }
    return rawStep * kPitchFractionScale;
  }

  // V4 keeps an 8-bit fractional accumulator, so the truncated step is in
  // 16.8 pitch units. The driver does not snap to the exact target at the end.
  return diff / static_cast<int32_t>(steps);
}

bool akaoSnesSupportsPitchEnvelope(AkaoSnesVersion version) {
  return version == AKAOSNES_V1 || version == AKAOSNES_V2;
}

uint16_t akaoSnesPitchEnvelopeProgressStep(AkaoSnesVersion version, uint8_t length) {
  if (length == 0) {
    return 0;
  }

  // V1 runs for exactly length updates with floor(65535 / length), then
  // holds the last computed offset, which is just short of the target. V2
  // uses floor($FF00 / length) and clamps to the target when progress overflows.
  return static_cast<uint16_t>((version == AKAOSNES_V1 ? 0xffff : 0xff00) / length);
}

int32_t akaoSnesPitchEnvelopeOffset(int32_t targetOffset, uint8_t progressHigh) {
  const int32_t targetMagnitude = targetOffset < 0 ? -targetOffset : targetOffset;
  int32_t currentMagnitude = (targetMagnitude / kPitchFractionScale) * progressHigh / 256;
  currentMagnitude *= kPitchFractionScale;
  return targetOffset < 0 ? -currentMagnitude : currentMagnitude;
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
  pitchSlideNoteValid = false;
  pitchSlideBaseNote = 0;
  pitchSlideCurrentNote = 0;
  pitchSlide.setPitchToCents(akaoSnesPitchCents);
  pitchSlide.reset(kDefaultPitchBendRangeCents);
}

void AkaoSnesTrack::updatePitchSlide() {
  advancePitchBendAutomation(pitchSlide);
}

void AkaoSnesTrack::updatePitchEnvelope() {
  const auto *parentSeq = static_cast<AkaoSnesSeq*>(this->parentSeq);
  if (!akaoSnesSupportsPitchEnvelope(parentSeq->version) ||
      !pitchEnvelope.active ||
      !pitchSlide.baseValid()) {
    return;
  }

  if (!pitchEnvelopeDelayElapsed()) {
    return;
  }

  int32_t currentOffset;
  if (!advancePitchEnvelopeTick(parentSeq->version, currentOffset)) {
    return;
  }

  pitchSlide.setCurrentPitch(pitchSlide.basePitch() + currentOffset);
  applyPitchBendAutomation(pitchSlide);
}

bool AkaoSnesTrack::pitchEnvelopeDelayElapsed() {
  // Both V1 and V2 update on sequencer ticks after note setup. A stored delay
  // value of 0 or 1 therefore allows the first movement on the next eligible
  // tick; larger values wait until the countdown reaches 1.
  if (pitchEnvelope.activeDelay > 1) {
    pitchEnvelope.activeDelay--;
    return false;
  }
  if (pitchEnvelope.activeDelay == 1) {
    pitchEnvelope.activeDelay = 0;
  }

  return true;
}

bool AkaoSnesTrack::advancePitchEnvelopeTick(AkaoSnesVersion version, int32_t& currentOffset) {
  if (version == AKAOSNES_V1 && pitchEnvelope.activeCount == 0) {
    pitchEnvelope.active = false;
    return false;
  }

  if (version == AKAOSNES_V1) {
    pitchEnvelope.activeCount--;
    pitchEnvelope.progress += pitchEnvelope.progressStep;
    const uint8_t progressHigh = static_cast<uint8_t>(pitchEnvelope.progress >> 8);
    currentOffset = akaoSnesPitchEnvelopeOffset(pitchEnvelope.targetOffset, progressHigh);
    if (pitchEnvelope.activeCount == 0) {
      pitchEnvelope.active = false;
    }
  }
  else if (pitchEnvelope.progress + pitchEnvelope.progressStep > 0xffff) {
    currentOffset = pitchEnvelope.targetOffset;
    pitchEnvelope.progress = 0x10000;
    pitchEnvelope.active = false;
  }
  else {
    pitchEnvelope.progress += pitchEnvelope.progressStep;
    const uint8_t progressHigh = static_cast<uint8_t>(pitchEnvelope.progress >> 8);
    currentOffset = akaoSnesPitchEnvelopeOffset(pitchEnvelope.targetOffset, progressHigh);
  }

  return true;
}

void AkaoSnesTrack::resetPitchBendForNewNote() {
  const auto *parentSeq = static_cast<AkaoSnesSeq*>(this->parentSeq);
  pitchSlide.clearMotion();
  pitchSlide.invalidateBase();
  pitchSlideNoteValid = false;

  if (!pitchSlide.atRest(kDefaultPitchBendRangeCents)) {
    if (akaoSnesSupportsPitchEnvelope(parentSeq->version) && pitchEnvelope.enabled) {
      setPitchBendAutomationBend(pitchSlide, 0);
      return;
    }
    resetPitchBendAutomation(pitchSlide, kDefaultPitchBendRangeCents);
  }
}

void AkaoSnesTrack::beginNotePitch(uint8_t note, bool validForPitchBend) {
  resetPitchBendForNewNote();
  if (validForPitchBend) {
    pitchSlideBaseNote = akaoSnesCorrectedNote(note, transpose);
    pitchSlideCurrentNote = pitchSlideBaseNote;
    pitchSlideNoteValid = true;
    pitchSlide.beginNote(kNominalDspPitch * kPitchFractionScale);
    beginPitchEnvelopeForNote();
  }
}

void AkaoSnesTrack::setPitchEnvelope(int8_t semitones, uint8_t delay, uint8_t length) {
  const auto *parentSeq = static_cast<AkaoSnesSeq*>(this->parentSeq);
  // An offset of zero disables the persistent envelope. We also treat zero
  // length as off for MIDI export, matching V2 and avoiding V1's no-motion case.
  if (semitones == 0 || length == 0) {
    clearPitchEnvelope();
    return;
  }

  pitchEnvelope.enabled = true;
  pitchEnvelope.semitones = semitones;
  pitchEnvelope.delay = delay;
  pitchEnvelope.length = length;
  pitchEnvelope.progressStep = akaoSnesPitchEnvelopeProgressStep(parentSeq->version, length);
}

void AkaoSnesTrack::clearPitchEnvelope() {
  // The driver off commands stop future envelope updates; they do not generate
  // a return-to-base bend. The current output naturally gets reset by the next
  // normal note setup.
  pitchEnvelope = {};
}

void AkaoSnesTrack::beginPitchEnvelopeForNote() {
  const auto *parentSeq = static_cast<AkaoSnesSeq*>(this->parentSeq);
  if (!akaoSnesSupportsPitchEnvelope(parentSeq->version) ||
      !pitchEnvelope.enabled ||
      !pitchSlide.baseValid()) {
    return;
  }

  // The target offset is relative to the note being started, after octave and
  // transpose have produced the current MIDI note. It is not a previous-note
  // glide. Ties intentionally do not call this path.
  const int32_t targetPitch = akaoSnesPitchForSemitoneOffset(pitchEnvelope.semitones);
  const int32_t rawDiff = (targetPitch - pitchSlide.basePitch()) / kPitchFractionScale;
  const int32_t rawMagnitude = rawDiff < 0 ? -rawDiff : rawDiff;
  const int32_t signedMagnitude = pitchEnvelope.semitones < 0 ? -rawMagnitude : rawMagnitude;
  pitchEnvelope.targetOffset = signedMagnitude * kPitchFractionScale;
  pitchEnvelope.activeDelay = pitchEnvelope.delay;
  pitchEnvelope.activeCount = parentSeq->version == AKAOSNES_V1 ? pitchEnvelope.length : 0;
  pitchEnvelope.progress = 0;
  pitchEnvelope.active = pitchEnvelope.targetOffset != 0 && pitchEnvelope.progressStep != 0;
  pitchSlide.setCurrentPitch(pitchSlide.basePitch());

  if (pitchEnvelope.active) {
    setPitchBendAutomationRange(
        pitchSlide,
        pitchSlide.rangeCentsForSlide(pitchSlide.basePitch(),
                                      pitchSlide.basePitch() + pitchEnvelope.targetOffset,
                                      kDefaultPitchBendRangeCents));
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
  // V3/V4 pitch slide commands are pending state. A following normal note
  // consumes the state after that note establishes its base pitch; a following
  // tie consumes it against the already-sounding pitch; a rest leaves it primed.
  if (pendingPitchSlideSteps == 0 || pendingPitchSlideSemitones == 0) {
    clearPendingPitchSlide();
    return;
  }

  const uint16_t steps = pendingPitchSlideSteps;
  const int8_t semitones = pendingPitchSlideSemitones;
  clearPendingPitchSlide();

  if (!pitchSlide.baseValid() || !pitchSlideNoteValid) {
    return;
  }

  pitchSlideCurrentNote = static_cast<int16_t>(pitchSlideCurrentNote + semitones);
  const int32_t currentPitch = pitchSlide.currentPitch();
  const int32_t targetPitch = akaoSnesPitchForSemitoneOffset(pitchSlideCurrentNote - pitchSlideBaseNote);
  const int32_t step = akaoSnesPitchSlideStep(parentSeq->version, currentPitch, targetPitch, steps);
  const int32_t finalPitch = currentPitch + (step * static_cast<int32_t>(steps));
  const uint16_t rangeCents = std::max(
      pitchSlide.rangeCentsForSlide(currentPitch, targetPitch, kDefaultPitchBendRangeCents),
      pitchSlide.rangeCentsForSlide(currentPitch, finalPitch, kDefaultPitchBendRangeCents));

  beginPitchBendAutomation(
      pitchSlide,
      SeqMotionPlan<int32_t>::targetOverTicksWithStep(finalPitch, step, steps),
      rangeCents,
      kDefaultPitchBendRangeCents,
      true);

  // Apply the first update on the same sequencer tick that consumes the setup command.
  updatePitchSlide();
}
