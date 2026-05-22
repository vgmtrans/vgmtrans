/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoSnesSeq.h"
#include "AkaoSnesModulation.h"
#include "automation/SeqTrackAutomation.h"
#include <algorithm>
#include <spdlog/fmt/fmt.h>

/*
 * AkaoSnes vibrato events are exported as MIDI LFO automation. The event
 * handlers here keep only the live per-track state; the version-specific math
 * lives in AkaoSnesModulation.h.
 *
 * V1/FF4, V3, and V4 have per-note fade-in behavior that restarts on normal
 * keyed notes and is not restarted by ties/slur/legato. V2 uses a different
 * operand order for the three-byte LFO setup command. V3/V4 share the later
 * command shape, with V4's rate byte treating zero as 256 frames.
 */

void AkaoSnesSeq::syncTempoDependentTracks() {
  if (!akao_snes::modulation::supportsLfoAutomation(version)) {
    return;
  }

  for (auto *track : aTracks) {
    static_cast<AkaoSnesTrack*>(track)->syncTempoDependentLfos();
  }
}

AkaoSnesTrack::LfoParams AkaoSnesTrack::readLfoParams() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  // V2 stores depth, delay, rate. The other supported versions store delay, rate, depth.
  if (parent->version == AKAOSNES_V2) {
    const uint8_t depth = readByte(curOffset++);
    const uint8_t delay = readByte(curOffset++);
    const uint8_t rate = readByte(curOffset++);
    return {delay, rate, depth};
  }

  const uint8_t delay = readByte(curOffset++);
  const uint8_t rate = readByte(curOffset++);
  const uint8_t depth = readByte(curOffset++);
  return {delay, rate, depth};
}

void AkaoSnesTrack::clearVibratoRateAndDelay() {
  addChannelPressureNoItem(0);
  addControllerEventNoItem(akao_snes::modulation::kVibratoDelayController, 0);
}

void AkaoSnesTrack::applyVibrato(uint32_t offset, uint32_t length, const LfoParams& params) {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  const bool active = akao_snes::modulation::isLfoActive(parent->version, params.rate, params.depth);

  addGenericEvent(offset,
                  length,
                  "Vibrato",
                  fmt::format("Delay: {}  Rate: {}  Depth: {}", params.delay, params.rate, params.depth),
                  Type::Vibrato);

  if (!akao_snes::modulation::supportsLfoAutomation(parent->version)) {
    return;
  }

  vibrato.configure(params.delay, params.rate, params.depth);
  configureVibratoFade();

  uint8_t midiDepth = active
      ? akao_snes::modulation::vibratoDepthMidiValue(parent->version, params.rate, params.depth)
      : 0;
  if (parent->version == AKAOSNES_V4 && active && vibrato.hasReusableFade()) {
    const uint32_t delay = akao_snes::modulation::delayTicks(parent->version, vibrato.delay());
    const int32_t initialDepth = vibrato.configuredDepth(8) / 4;
    vibrato.beginReusableFade(delay, vibrato.configuredDepth(8), initialDepth);
    midiDepth = (delay == 0) ? vibratoFadeDepthMidiValue(initialDepth) : 0;
  }
  setSynthLfoModulationDepth(vibrato, midiDepth, true);
  if (active) {
    syncVibratoRateAndDelay();
  }
  else {
    clearVibratoRateAndDelay();
  }
}

void AkaoSnesTrack::clearVibrato(uint32_t offset, uint32_t length) {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);

  addGenericEvent(offset, length, "Vibrato Off", "", Type::Vibrato);

  if (!akao_snes::modulation::supportsLfoAutomation(parent->version)) {
    return;
  }

  vibrato.setDepth(0);
  // Turning vibrato off also prevents later notes from replaying the stored fade-in ramp.
  vibrato.clearReusableFade();
  setSynthLfoModulationDepth(vibrato, 0, true);
  clearVibratoRateAndDelay();
}

void AkaoSnesTrack::configureVibratoFade() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (!akao_snes::modulation::isLfoActive(parent->version, vibrato.rate(), vibrato.depth())) {
    vibrato.clearReusableFade();
    return;
  }

  if (parent->version == AKAOSNES_V1) {
    // V1/FF4 ramps from zero to the configured vibrato depth over a driver-derived
    // number of sequencer ticks. The actual MIDI depth is computed during the
    // fade so tempo changes can keep the reusable shape synchronized.
    vibrato.setReusableFadeToConfiguredDepth(
        akao_snes::modulation::v1VibratoRampTicks(vibrato.rate(), parent->tempo), 8);
    return;
  }

  if (parent->version == AKAOSNES_V3 && vibrato.delay() != 0) {
    vibrato.setReusableFadeToConfiguredDepth(
        akao_snes::modulation::v3VibratoRampTicks(vibrato.rate(), parent->tempo), 8);
    return;
  }

  if (parent->version == AKAOSNES_V4 && vibrato.delay() != 0) {
    const uint32_t ticks = akao_snes::modulation::v4VibratoRampTicks(vibrato.rate(), parent->tempo);
    const int32_t targetDepth = vibrato.configuredDepth(8);
    const int32_t initialDepth = targetDepth / 4;
    const int32_t step = ticks == 0 ? 0 : (targetDepth - initialDepth) / static_cast<int32_t>(ticks);
    vibrato.setReusableFade(ticks, step);
    return;
  }

  vibrato.clearReusableFade();
}

void AkaoSnesTrack::beginVibratoForNote() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  const bool supportsNoteStartFade =
      parent->version == AKAOSNES_V1 || parent->version == AKAOSNES_V3 || parent->version == AKAOSNES_V4;
  if (!supportsNoteStartFade || !vibrato.hasReusableFade() ||
      !akao_snes::modulation::isLfoActive(parent->version, vibrato.rate(), vibrato.depth())) {
    return;
  }

  // Normal notes with a reusable fade restart the exported depth ramp. V4 begins
  // at quarter depth when there is no effective pre-delay; the others begin at
  // zero. Ties and legato notes skip this call.
  const uint32_t delay = akao_snes::modulation::delayTicks(parent->version, vibrato.delay());
  const int32_t initialDepth = (parent->version == AKAOSNES_V4)
      ? vibrato.configuredDepth(8) / 4
      : 0;
  vibrato.beginReusableFade(delay, vibrato.configuredDepth(8), initialDepth);
  setSynthLfoModulationDepth(vibrato,
                             (delay == 0) ? vibratoFadeDepthMidiValue(initialDepth) : 0,
                             true);
}

uint8_t AkaoSnesTrack::vibratoFadeDepthMidiValue(int32_t depth) const {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  const int32_t targetDepth = vibrato.configuredDepth(8);
  if (targetDepth <= 0) {
    return 0;
  }

  const int fullDepth = akao_snes::modulation::vibratoDepthMidiValue(parent->version,
                                                                     vibrato.rate(),
                                                                     vibrato.depth());
  return static_cast<uint8_t>(std::clamp<int>((fullDepth * depth + (targetDepth / 2)) / targetDepth,
                                              0,
                                              127));
}

void AkaoSnesTrack::updateVibratoFade() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (parent->version != AKAOSNES_V1 && parent->version != AKAOSNES_V3 &&
      parent->version != AKAOSNES_V4) {
    return;
  }

  advanceSynthLfoFadeToModulation(vibrato, 8, [this](int32_t depth) {
    return vibratoFadeDepthMidiValue(depth);
  });
}

void AkaoSnesTrack::syncVibratoRateAndDelay() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (!akao_snes::modulation::supportsLfoAutomation(parent->version) ||
      !akao_snes::modulation::isLfoActive(parent->version, vibrato.rate(), vibrato.depth())) {
    return;
  }

  // Tempo changes can alter reusable fade duration, so refresh it when
  // resending tempo-dependent LFO controller values.
  configureVibratoFade();

  const uint8_t rateMidiValue = akao_snes::modulation::rateMidiValue(parent->version,
                                                                     vibrato.rate(),
                                                                     vibrato.depth(),
                                                                     parent->TIMER0_FREQUENCY);
  addChannelPressureNoItem(rateMidiValue);
  addControllerEventNoItem(akao_snes::modulation::kVibratoDelayController,
                           akao_snes::modulation::delayMidiValue(parent->version,
                                                                 vibrato.delay(),
                                                                 parent->tempo,
                                                                 parent->TIMER0_FREQUENCY));
}

void AkaoSnesTrack::syncTempoDependentLfos() {
  syncVibratoRateAndDelay();
}
