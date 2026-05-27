/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "base/types.h"
#include "AkaoSnesSeq.h"
#include "AkaoSnesModulation.h"
#include "automation/SeqTrackAutomation.h"
#include <algorithm>
#include <spdlog/fmt/fmt.h>

/*
 * AkaoSnes LFO events cover vibrato and tremolo across all four driver
 * versions. The event handlers here keep only the live per-track state and
 * convert it to MIDI automation; the version-specific math lives in
 * AkaoSnesModulation.cpp.
 *
 * V1/FF4 and V3 have per-note fade-in behavior that restarts on normal keyed
 * notes and is not restarted by ties/slur/legato. V4 keeps that behavior for
 * vibrato only; delayed V4 tremolo runs at quarter depth. V2 is the only
 * supported version that orders the three-byte LFO setup as depth, delay, rate.
 */

void AkaoSnesSeq::syncTempoDependentTracks() {
  for (auto *track : aTracks) {
    static_cast<AkaoSnesTrack*>(track)->syncTempoDependentLfos();
  }
}

AkaoSnesTrack::LfoParams AkaoSnesTrack::readLfoParams() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  // V2 stores depth, delay, rate. The other supported versions store delay, rate, depth.
  if (parent->version == AKAOSNES_V2) {
    const u8 depth = readByte(curOffset++);
    const u8 delay = readByte(curOffset++);
    const u8 rate = readByte(curOffset++);
    return {delay, rate, depth};
  }

  const u8 delay = readByte(curOffset++);
  const u8 rate = readByte(curOffset++);
  const u8 depth = readByte(curOffset++);
  return {delay, rate, depth};
}

void AkaoSnesTrack::setLfoOutputDepth(LfoTarget target, u8 depth, bool force) {
  auto& lfo = (target == LfoTarget::Vibrato) ? vibrato : tremolo;
  if (target == LfoTarget::Vibrato) {
    emitVibratoDepth(lfo, depth, force);
  }
  else {
    emitTremoloDepth(lfo, depth, force);
  }
}

void AkaoSnesTrack::clearLfoRateAndDelay(LfoTarget target) {
  if (target == LfoTarget::Vibrato) {
    addVibratoFrequencyNoItem(0);
    addVibratoDelayNoItem(0);
  }
  else {
    addTremoloFrequencyNoItem(0);
    addTremoloDelayNoItem(0);
  }
}

void AkaoSnesTrack::applyLfo(LfoTarget target,
                             u32 offset,
                             u32 length,
                             const LfoParams& params) {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  const bool isVibrato = target == LfoTarget::Vibrato;
  const bool supported = isVibrato || akao_snes::modulation::exportsTremolo(parent->version);
  const bool active = supported && akao_snes::modulation::isLfoActive(parent->version, params.rate, params.depth);

  addGenericEvent(offset,
                  length,
                  isVibrato ? "Vibrato" : "Tremolo",
                  fmt::format("Delay: {}  Rate: {}  Depth: {}", params.delay, params.rate, params.depth),
                  isVibrato ? Type::Vibrato : Type::Tremelo);

  auto& lfo = isVibrato ? vibrato : tremolo;
  lfo.configure(params.delay, params.rate, params.depth);
  if (isVibrato) {
    configureVibratoFade();
  }
  else {
    configureTremoloFade();
  }

  u8 midiDepth = active
      ? (isVibrato
             ? akao_snes::modulation::vibratoDepthMidiValue(parent->version, params.rate, params.depth)
             : akao_snes::modulation::tremoloDepthMidiValue(parent->version,
                                                            params.rate,
                                                            params.depth,
                                                            params.delay))
      : 0;
  if (isVibrato && parent->version == AKAOSNES_V4 && active && vibrato.hasReusableFade()) {
    const u32 delay = akao_snes::modulation::delayTicks(parent->version, vibrato.delay());
    const s32 initialDepth = vibrato.configuredDepth(8) / 4;
    vibrato.beginReusableFade(delay, vibrato.configuredDepth(8), initialDepth);
    midiDepth = (delay == 0) ? vibratoFadeDepthMidiValue(initialDepth) : 0;
  }
  setLfoOutputDepth(target, midiDepth, true);
  if (active) {
    syncLfoRateAndDelay(target);
  }
  else {
    clearLfoRateAndDelay(target);
  }
}

void AkaoSnesTrack::clearLfo(LfoTarget target, u32 offset, u32 length) {
  const bool isVibrato = target == LfoTarget::Vibrato;

  addGenericEvent(offset,
                  length,
                  isVibrato ? "Vibrato Off" : "Tremolo Off",
                  "",
                  isVibrato ? Type::Vibrato : Type::Tremelo);

  auto& lfo = isVibrato ? vibrato : tremolo;
  lfo.setDepth(0);
  // V3 off commands stop future LFO updates without immediately clearing the current output.
  // Converted synth LFOs cannot freeze at the last output value, so stop modulation here by emitting depth 0.
  lfo.clearReusableFade();
  setLfoOutputDepth(target, 0, true);
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
        akao_snes::modulation::v3LfoRampTicks(vibrato.rate(), parent->tempo), 8);
    return;
  }

  if (parent->version == AKAOSNES_V4 && vibrato.delay() != 0) {
    const u32 ticks = akao_snes::modulation::v4VibratoRampTicks(vibrato.rate(), parent->tempo);
    const s32 targetDepth = vibrato.configuredDepth(8);
    const s32 initialDepth = targetDepth / 4;
    const s32 step = ticks == 0 ? 0 : (targetDepth - initialDepth) / static_cast<s32>(ticks);
    vibrato.setReusableFade(ticks, step);
    return;
  }

  vibrato.clearReusableFade();
}

void AkaoSnesTrack::configureTremoloFade() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (parent->version != AKAOSNES_V3 ||
      !akao_snes::modulation::isLfoActive(parent->version, tremolo.rate(), tremolo.depth()) ||
      tremolo.delay() == 0) {
    tremolo.clearReusableFade();
    return;
  }

  tremolo.setReusableFadeToConfiguredDepth(
      akao_snes::modulation::v3LfoRampTicks(tremolo.rate(), parent->tempo), 8);
}

void AkaoSnesTrack::beginVibratoForNote() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (parent->version == AKAOSNES_V2 || !vibrato.hasReusableFade() ||
      !akao_snes::modulation::isLfoActive(parent->version, vibrato.rate(), vibrato.depth())) {
    return;
  }

  // Normal notes with a reusable fade restart the exported depth ramp. V4 begins
  // at quarter depth when there is no effective pre-delay; the others begin at
  // zero. Ties and legato notes skip this call.
  const u32 delay = akao_snes::modulation::delayTicks(parent->version, vibrato.delay());
  const s32 initialDepth = (parent->version == AKAOSNES_V4)
      ? vibrato.configuredDepth(8) / 4
      : 0;
  vibrato.beginReusableFade(delay, vibrato.configuredDepth(8), initialDepth);
  emitVibratoDepth(vibrato,
                   (delay == 0) ? vibratoFadeDepthMidiValue(initialDepth) : 0,
                   true);
}

void AkaoSnesTrack::beginTremoloForNote() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (parent->version != AKAOSNES_V3 || !tremolo.hasReusableFade()) {
    return;
  }

  tremolo.beginReusableFadeToConfiguredDepth(8);
  emitTremoloDepth(tremolo, 0, true);
}

u8 AkaoSnesTrack::vibratoFadeDepthMidiValue(s32 depth) const {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  const s32 targetDepth = vibrato.configuredDepth(8);
  if (targetDepth <= 0) {
    return 0;
  }

  const int fullDepth = akao_snes::modulation::vibratoDepthMidiValue(parent->version,
                                                                     vibrato.rate(),
                                                                     vibrato.depth());
  return static_cast<u8>(std::clamp<int>((fullDepth * depth + (targetDepth / 2)) / targetDepth,
                                              0,
                                              127));
}

u8 AkaoSnesTrack::tremoloFadeDepthMidiValue(s32 depth) const {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  const s32 targetDepth = tremolo.configuredDepth(8);
  if (targetDepth <= 0) {
    return 0;
  }

  const int fullDepth = akao_snes::modulation::tremoloDepthMidiValue(parent->version,
                                                                     tremolo.rate(),
                                                                     tremolo.depth());
  return static_cast<u8>(std::clamp<int>((fullDepth * depth + (targetDepth / 2)) / targetDepth,
                                              0,
                                              127));
}

void AkaoSnesTrack::updateVibratoFade() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  if (parent->version == AKAOSNES_V2) {
    return;
  }

  advanceVibratoDepthFade(vibrato, 8, [this](s32 depth) {
    return vibratoFadeDepthMidiValue(depth);
  });
}

void AkaoSnesTrack::updateTremoloFade() {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  // Only V3 implements a tremolo fade.
  if (parent->version != AKAOSNES_V3) {
    return;
  }

  advanceTremoloDepthFade(tremolo, 8, [this](s32 depth) {
    return tremoloFadeDepthMidiValue(depth);
  });
}

void AkaoSnesTrack::syncLfoRateAndDelay(LfoTarget target) {
  const auto *parent = static_cast<AkaoSnesSeq*>(parentSeq);
  const bool isVibrato = target == LfoTarget::Vibrato;
  auto& lfo = isVibrato ? vibrato : tremolo;
  if (!akao_snes::modulation::isLfoActive(parent->version, lfo.rate(), lfo.depth())) {
    return;
  }

  if (isVibrato) {
    // Tempo changes can alter reusable fade duration, so refresh it when
    // resending tempo-dependent LFO controller values.
    configureVibratoFade();
  }
  else {
    configureTremoloFade();
  }

  const u8 rateMidiValue = akao_snes::modulation::rateMidiValue(parent->version,
                                                                     lfo.rate(),
                                                                     lfo.depth(),
                                                                     parent->TIMER0_FREQUENCY);
  const u8 delayMidiValue = akao_snes::modulation::delayMidiValue(parent->version,
                                                                       lfo.delay(),
                                                                       parent->tempo,
                                                                       parent->TIMER0_FREQUENCY);
  if (isVibrato) {
    addVibratoFrequencyNoItem(rateMidiValue);
    addVibratoDelayNoItem(delayMidiValue);
  }
  else {
    addTremoloFrequencyNoItem(rateMidiValue);
    addTremoloDelayNoItem(delayMidiValue);
  }
}

void AkaoSnesTrack::syncTempoDependentLfos() {
  syncLfoRateAndDelay(LfoTarget::Vibrato);
  syncLfoRateAndDelay(LfoTarget::Tremolo);
}
