#include "NinSnesSeq.h"
#include "NinSnesVibrato.h"
#include "Modulation.h"
#include <algorithm>
#include <cmath>

namespace {

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

}  // namespace

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
  state.vibrato.advanceFadeAndApplyClamped(0, [this](int32_t depth) {
    setVibratoDepth(static_cast<uint8_t>(depth));
  });
}

void NinSnesTrack::setVibratoDepth(uint8_t depth) {
  auto& vibrato = state.vibrato;
  vibrato.setCurrentDepth(depth);
  const uint8_t midiDepth = convertVibratoDepthToMidi(depth, seq().maxVibratoDepthCents);
  setSynthLfoModulationDepth(vibrato, midiDepth);
}

void NinSnesTrack::clearVibratoRateAndDelay() {
  addChannelPressureNoItem(0);
  addControllerEventNoItem(nin_snes::vibrato::kDelayController, 0);
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
