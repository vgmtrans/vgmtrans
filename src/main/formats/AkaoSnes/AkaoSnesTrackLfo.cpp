/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoSnesSeq.h"
#include "AkaoSnesModulation.h"
#include "automation/SeqTrackAutomation.h"
#include <spdlog/fmt/fmt.h>

/*
 * AkaoSnes vibrato events are exported as MIDI LFO automation. The event
 * handlers here keep only the live per-track state; the version-specific math
 * lives in AkaoSnesModulation.h.
 *
 * V1/FF4 is the outlier: vibrato has a per-note fade-in behavior that restarts
 * on normal keyed notes and is not restarted by ties/slur/legato. V2 also uses
 * a different operand order for the three-byte LFO setup command. V3/V4 share
 * the later command shape, with V4's rate byte treating zero as 256 frames.
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
  // Only V1 creates reusable note-start vibrato fade state. Later versions
  // apply the configured vibrato depth directly after the setup command.
  configureVibratoFade();

  const uint8_t midiDepth = active
      ? akao_snes::modulation::vibratoDepthMidiValue(parent->version, params.rate, params.depth)
      : 0;
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
  // Turning V1 vibrato off also prevents later notes from replaying the stored fade-in ramp.
  vibrato.clearReusableFade();
  setSynthLfoModulationDepth(vibrato, 0, true);
  clearVibratoRateAndDelay();
}

void AkaoSnesTrack::configureVibratoFade() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (parent->version != AKAOSNES_V1 ||
      !akao_snes::modulation::isLfoActive(parent->version, vibrato.rate(), vibrato.depth())) {
    vibrato.clearReusableFade();
    return;
  }

  // V1/FF4 ramps from zero to the configured vibrato depth over a driver-derived
  // number of sequencer ticks. The actual MIDI depth is computed during the
  // fade so tempo changes can keep the reusable shape synchronized.
  vibrato.setReusableFadeToConfiguredDepth(
      akao_snes::modulation::v1VibratoRampTicks(vibrato.rate(), parent->tempo), 8);
}

void AkaoSnesTrack::beginVibratoForNote() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (parent->version != AKAOSNES_V1 || !vibrato.hasReusableFade() ||
      !akao_snes::modulation::isLfoActive(parent->version, vibrato.rate(), vibrato.depth())) {
    return;
  }

  // A normal V1 note starts with zero vibrato depth, then updateVibratoFade()
  // advances it on sequencer ticks. Ties and legato notes skip this call.
  vibrato.beginReusableFade(akao_snes::modulation::delayTicks(parent->version, vibrato.delay()),
                            vibrato.configuredDepth(8));
  setSynthLfoModulationDepth(vibrato, 0, true);
}

void AkaoSnesTrack::updateVibratoFade() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (parent->version != AKAOSNES_V1) {
    return;
  }

  advanceSynthLfoFadeToModulation(vibrato, 8, [this, parent](int32_t depth) {
    const int32_t targetDepth = vibrato.configuredDepth(8);
    if (targetDepth <= 0) {
      return 0;
    }

    const int fullDepth = akao_snes::modulation::vibratoDepthMidiValue(parent->version,
                                                                       vibrato.rate(),
                                                                       vibrato.depth());
    return static_cast<int>((fullDepth * depth + (targetDepth / 2)) / targetDepth);
  });
}

void AkaoSnesTrack::syncVibratoRateAndDelay() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (!akao_snes::modulation::supportsLfoAutomation(parent->version) ||
      !akao_snes::modulation::isLfoActive(parent->version, vibrato.rate(), vibrato.depth())) {
    return;
  }

  // Tempo changes can alter V1's reusable fade duration, so refresh it when
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
