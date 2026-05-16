/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoSnesSeq.h"
#include "AkaoSnesModulation.h"
#include "automation/SeqTrackAutomation.h"
#include <spdlog/fmt/fmt.h>

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

void AkaoSnesTrack::setLfoOutputDepth(LfoTarget target, uint8_t depth, bool force) {
  auto& lfo = (target == LfoTarget::Vibrato) ? vibrato : tremolo;
  if (target == LfoTarget::Vibrato) {
    setSynthLfoModulationDepth(lfo, depth, force);
  }
  else {
    setSynthLfoControllerDepth(lfo, akao_snes::modulation::kTremoloDepthController, depth, force);
  }
}

void AkaoSnesTrack::clearLfoRateAndDelay(LfoTarget target) {
  if (target == LfoTarget::Vibrato) {
    addChannelPressureNoItem(0);
    addControllerEventNoItem(akao_snes::modulation::kVibratoDelayController, 0);
  }
  else {
    addControllerEventNoItem(akao_snes::modulation::kTremoloRateController, 0);
    addControllerEventNoItem(akao_snes::modulation::kTremoloDelayController, 0);
  }
}

void AkaoSnesTrack::applyLfo(LfoTarget target,
                             uint32_t offset,
                             uint32_t length,
                             const LfoParams& params) {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  const bool isVibrato = target == LfoTarget::Vibrato;
  const bool active = akao_snes::modulation::isLfoActive(parent->version, params.rate, params.depth);

  addGenericEvent(offset,
                  length,
                  isVibrato ? "Vibrato" : "Tremolo",
                  isVibrato
                      ? fmt::format("Delay: {}  Rate: {}  Depth: {}", params.delay, params.rate, params.depth)
                      : fmt::format("Delay {}  Rate: {}  Depth {}", params.delay, params.rate, params.depth),
                  isVibrato ? Type::Vibrato : Type::Tremelo);

  if (!akao_snes::modulation::supportsLfoAutomation(parent->version)) {
    return;
  }

  auto& lfo = isVibrato ? vibrato : tremolo;
  lfo.configure(params.delay, params.rate, params.depth);
  if (isVibrato) {
    configureVibratoFade();
  }

  const uint8_t midiDepth = active
      ? (isVibrato
             ? akao_snes::modulation::vibratoDepthMidiValue(parent->version, params.rate, params.depth)
             : akao_snes::modulation::tremoloDepthMidiValue(parent->version, params.rate, params.depth))
      : 0;
  setLfoOutputDepth(target, midiDepth, true);
  if (active) {
    syncLfoRateAndDelay(target);
  }
  else {
    clearLfoRateAndDelay(target);
  }
}

void AkaoSnesTrack::clearLfo(LfoTarget target, uint32_t offset, uint32_t length) {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  const bool isVibrato = target == LfoTarget::Vibrato;

  addGenericEvent(offset,
                  length,
                  isVibrato ? "Vibrato Off" : "Tremolo Off",
                  "",
                  isVibrato ? Type::Vibrato : Type::Tremelo);

  if (!akao_snes::modulation::supportsLfoAutomation(parent->version)) {
    return;
  }

  auto& lfo = isVibrato ? vibrato : tremolo;
  lfo.setDepth(0);
  if (isVibrato) {
    vibrato.clearReusableFade();
  }
  setLfoOutputDepth(target, 0, true);
  clearLfoRateAndDelay(target);
}

void AkaoSnesTrack::configureVibratoFade() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (parent->version != AKAOSNES_V1 ||
      !akao_snes::modulation::isLfoActive(parent->version, vibrato.rate(), vibrato.depth())) {
    vibrato.clearReusableFade();
    return;
  }

  vibrato.setReusableFadeToConfiguredDepth(
      akao_snes::modulation::v1VibratoRampTicks(vibrato.rate(), parent->tempo), 8);
}

void AkaoSnesTrack::beginVibratoForNote() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (parent->version != AKAOSNES_V1 || !vibrato.hasReusableFade() ||
      !akao_snes::modulation::isLfoActive(parent->version, vibrato.rate(), vibrato.depth())) {
    return;
  }

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

void AkaoSnesTrack::syncLfoRateAndDelay(LfoTarget target) {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  const bool isVibrato = target == LfoTarget::Vibrato;
  auto& lfo = isVibrato ? vibrato : tremolo;
  if (!akao_snes::modulation::supportsLfoAutomation(parent->version) ||
      !akao_snes::modulation::isLfoActive(parent->version, lfo.rate(), lfo.depth())) {
    return;
  }

  if (isVibrato) {
    configureVibratoFade();
  }

  const uint8_t rateMidiValue = akao_snes::modulation::rateMidiValue(parent->version,
                                                                     lfo.rate(),
                                                                     lfo.depth(),
                                                                     parent->TIMER0_FREQUENCY);
  if (isVibrato) {
    addChannelPressureNoItem(rateMidiValue);
  }
  else {
    addControllerEventNoItem(akao_snes::modulation::kTremoloRateController, rateMidiValue);
  }

  addControllerEventNoItem(isVibrato
                               ? akao_snes::modulation::kVibratoDelayController
                               : akao_snes::modulation::kTremoloDelayController,
                           akao_snes::modulation::delayMidiValue(parent->version,
                                                                 lfo.delay(),
                                                                 parent->tempo,
                                                                 parent->TIMER0_FREQUENCY));
}

void AkaoSnesTrack::syncTempoDependentLfos() {
  syncLfoRateAndDelay(LfoTarget::Vibrato);
  syncLfoRateAndDelay(LfoTarget::Tremolo);
}
