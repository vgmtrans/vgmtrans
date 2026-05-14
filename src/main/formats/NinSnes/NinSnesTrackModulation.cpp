#include "NinSnesSeq.h"
#include "NinSnesVibrato.h"
#include "Modulation.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr uint16_t kNinSnesDefaultPitchBendRangeCents =
    NinSnesTrackState::kDefaultPitchBendRangeCents;

uint8_t convertVibratoDepthToMidi(uint8_t depth, double maxDepthCents) {
  if (depth == 0) {
    return 0;
  }

  const int midiValue = static_cast<int>(std::lround(128.0 * nin_snes::vibrato::depthCents(depth) / maxDepthCents));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

uint8_t convertVibratoRateToMidi(uint8_t rate, double tempo, double maxRateHz) {
  const double currentRateHz = nin_snes::vibrato::rateHz(rate, tempo);
  if (currentRateHz <= 0.0) {
    return 0;
  }

  return midiValueForHertzInRange(currentRateHz, nin_snes::vibrato::kMinRateHz, maxRateHz);
}

uint8_t convertVibratoDelayToMidi(uint8_t delay, double tempo) {
  return midiValueForSecondsInRange(nin_snes::vibrato::delaySeconds(delay, tempo),
                                    nin_snes::vibrato::kMinDelaySeconds,
                                    nin_snes::vibrato::kMaxDelaySeconds);
}

int32_t notePitch(uint8_t note) {
  // NinSnes stores slide pitch in semitone units with an 8-bit fractional part.
  return static_cast<int32_t>(note & 0x7f) << 8;
}

}  // namespace

NinSnesTrack::PitchSlideEvent NinSnesTrack::readPitchSlide(uint32_t offset) {
  return PitchSlideEvent {
    offset,
    4,
    readByte(curOffset++),
    readByte(curOffset++),
    readByte(curOffset++),
  };
}

bool NinSnesTrack::consumeQueuedPitchSlide() {
  if (state.pitch.motionTicksRemaining() != 0) {
    return false;
  }

  const auto statusByte = readByte(curOffset);
  auto nextEvent = seq().EventMap.find(statusByte);
  if (nextEvent == seq().EventMap.end() || nextEvent->second != EVENT_PITCH_SLIDE) {
    return false;
  }

  const auto slideOffset = curOffset++;
  auto slide = readPitchSlide(slideOffset);
  addPitchSlideEvent(slide);
  beginPitchSlide(slide);
  return true;
}

void NinSnesTrack::addPitchSlideEvent(const PitchSlideEvent& slide) {
  auto desc = fmt::format("Delay: {:d}  Length: {:d}  Target Note: {:d}",
                          slide.delay,
                          slide.length,
                          slide.targetNote & 0x7f);
  addGenericEvent(slide.offset, slide.eventLength, "Pitch Slide", desc, Type::PitchBendSlide);
}

void NinSnesTrack::beginPitchSlide(const PitchSlideEvent& slide) {
  state.pitch.clearMotion();
  activatePitchMotion(slide.delay, slide.length, notePitch(slide.targetNote));
}

void NinSnesTrack::activatePitchMotion(uint8_t delay, uint8_t length, int32_t targetPitch) {
  auto& pitch = state.pitch;
  if (!pitch.baseValid() || length == 0) {
    return;
  }

  // F1/F2 and F9 all reduce to the same live pitch-motion state: wait for an optional delay,
  // advance by a signed 8.8 delta each tick, then snap exactly to the stored target.
  const int32_t currentPitch = pitch.currentPitch();
  beginPitchBendAutomation(
      pitch,
      pitch.motionToTarget(targetPitch, length, delay),
      pitch.rangeCentsForSlide(currentPitch, targetPitch, kNinSnesDefaultPitchBendRangeCents),
      true);
}

void NinSnesTrack::updatePitchSlide() {
  auto& pitch = state.pitch;
  if (!pitch.motionActive()) {
    return;
  }

  advancePitchBendAutomation(pitch);
}

void NinSnesTrack::beginNotePitch(uint8_t note) {
  resetPitchBendForNewNote();
  state.pitch.beginNote(notePitch(note));
  activateStoredPitchEnvelope();
  beginNoteVibrato();
}

void NinSnesTrack::activateStoredPitchEnvelope() {
  const auto& pitchEnvelope = state.pitchEnvelope;
  if (!pitchEnvelope.enabled()) {
    return;
  }

  const int32_t semitoneOffset = static_cast<int32_t>(pitchEnvelope.semitones) * 256;
  int32_t targetPitch = state.pitch.basePitch();
  if (pitchEnvelope.mode == NinSnesTrackState::StoredPitchEnvelope::Mode::To) {
    targetPitch += semitoneOffset;
  } else {
    state.pitch.setCurrentPitch(state.pitch.basePitch() - semitoneOffset);
  }

  activatePitchMotion(pitchEnvelope.delay, pitchEnvelope.length, targetPitch);
}

void NinSnesTrack::beginNoteVibrato() {
  // EVENT_VIBRATO_FADE is a reusable per-note fade-in for the configured E3 vibrato.
  if (state.vibrato.active() && state.vibrato.beginReusableFadeToConfiguredDepth()) {
    setVibratoDepth(0);
  }
}

void NinSnesTrack::applyConfiguredVibrato() {
  auto& vibrato = state.vibrato;
  const bool active = vibrato.active();
  if (active) {
    auto& parentSeq = seq();
    if (readMode != READMODE_CONVERT_TO_MIDI) {
      parentSeq.maxVibratoDepthCents =
          std::max(parentSeq.maxVibratoDepthCents, nin_snes::vibrato::depthCents(vibrato.depth()));
    }
  }

  setVibratoDepth(vibrato.outputDepthWhen(active));
  if (active) {
    syncVibratoRateAndDelay();
  } else {
    clearVibratoRateAndDelay();
  }
}

void NinSnesTrack::updateVibratoFade() {
  advanceSynthLfoFadeToModulation(state.vibrato, 0, [this](int32_t depth) {
    return convertVibratoDepthToMidi(static_cast<uint8_t>(depth), seq().maxVibratoDepthCents);
  });
}

void NinSnesTrack::setVibratoDepth(uint8_t depth) {
  auto& vibrato = state.vibrato;
  vibrato.setCurrentDepthPreservingMotion(depth);
  const uint8_t midiDepth = convertVibratoDepthToMidi(depth, seq().maxVibratoDepthCents);
  setSynthLfoModulationDepth(vibrato, midiDepth);
}

void NinSnesTrack::clearVibratoRateAndDelay() {
  addChannelPressureNoItem(0);
  addControllerEventNoItem(nin_snes::vibrato::kDelayController, 0);
}

void NinSnesTrack::resetPitchBendForNewNote() {
  state.pitch.clearMotion();
  state.pitch.invalidateBase();
  if (state.pitch.atRest(kNinSnesDefaultPitchBendRangeCents)) {
    return;
  }

  resetPitchBendAutomation(state.pitch, kNinSnesDefaultPitchBendRangeCents);
}

void NinSnesTrack::syncVibratoRateAndDelay() {
  const auto& vibrato = state.vibrato;
  if (!vibrato.active()) {
    return;
  }

  auto& parentSeq = seq();
  const double currentTempo = parentSeq.tempo;
  if (readMode != READMODE_CONVERT_TO_MIDI) {
    // Rate and delay are both tempo-relative, so a sequence-specific max only makes sense once the
    // current tempo has been folded into the exported Hz value.
    parentSeq.maxVibratoRateHz =
        std::max(parentSeq.maxVibratoRateHz, nin_snes::vibrato::rateHz(vibrato.rate(), currentTempo));
  }

  addChannelPressureNoItem(convertVibratoRateToMidi(vibrato.rate(), currentTempo, parentSeq.maxVibratoRateHz));
  addControllerEventNoItem(nin_snes::vibrato::kDelayController,
                           convertVibratoDelayToMidi(vibrato.delay(), currentTempo));
}
