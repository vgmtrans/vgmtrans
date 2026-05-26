/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "util/types.h"

#include "SeqAutomation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

// Stores source-space pitch and converts it to MIDI pitch bend when emitting.
template <typename PitchType>
class SeqPitchBendAutomation {
 public:
  using PitchToCents = std::function<double(PitchType, PitchType)>;

  explicit SeqPitchBendAutomation(double centsPerPitchUnit = 100.0)
      : m_centsPerPitchUnit(centsPerPitchUnit) {}

  void reset(u16 defaultRangeCents) {
    m_baseValid = false;
    m_basePitch = {};
    m_pitch.reset();
    m_pitchBendRangeCents = defaultRangeCents;
    m_currentPitchBend = 0;
  }

  void setCentsPerPitchUnit(double centsPerPitchUnit) {
    m_centsPerPitchUnit = centsPerPitchUnit;
  }

  void setPitchToCents(PitchToCents pitchToCents) {
    m_pitchToCents = pitchToCents;
  }

  // Set the note-relative base pitch and reset current pitch to that base.
  void beginNote(PitchType basePitch) {
    m_baseValid = true;
    m_basePitch = basePitch;
    m_pitch.setCurrent(basePitch);
  }

  // Mark the current note as ineligible for pitch bend automation.
  void invalidateBase() { m_baseValid = false; }
  [[nodiscard]] bool baseValid() const { return m_baseValid; }

  void clearMotion() { m_pitch.clearMotion(); }
  void setCurrentPitch(PitchType pitch) { m_pitch.setCurrent(pitch); }
  // Replace current pitch without cancelling an active slide.
  void setCurrentPitchPreservingMotion(PitchType pitch) { m_pitch.setCurrentPreservingMotion(pitch); }

  [[nodiscard]] PitchType basePitch() const { return m_basePitch; }
  [[nodiscard]] PitchType currentPitch() const { return m_pitch.current(); }
  [[nodiscard]] bool motionActive() const { return m_pitch.active(); }
  [[nodiscard]] u32 motionTicksRemaining() const { return m_pitch.ticksRemaining(); }

  // Compute the source-space step needed to reach targetPitch over the given tick count.
  [[nodiscard]] PitchType stepToTarget(PitchType targetPitch, u32 ticks) const {
    if (ticks == 0) {
      return {};
    }
    return static_cast<PitchType>((targetPitch - currentPitch()) /
                                  static_cast<PitchType>(ticks));
  }

  // Build a target-over-ticks motion plan in source pitch units.
  [[nodiscard]] SeqMotionPlan<PitchType> motionToTarget(PitchType targetPitch,
                                                        u32 ticks,
                                                        u32 delay = 0) const {
    return SeqMotionPlan<PitchType>::targetOverTicks(targetPitch, ticks, delay);
  }

  // Return a MIDI bend range large enough for pitchUnits, but not below minimumRangeCents.
  [[nodiscard]] u16 rangeCentsForPitchSpan(double pitchUnits,
                                                u16 minimumRangeCents) const {
    const auto cents = static_cast<u16>(
        std::ceil(std::abs(pitchUnits) * m_centsPerPitchUnit));
    return std::max<u16>(minimumRangeCents, cents);
  }

  // Return a range large enough for both slide endpoints relative to the base pitch.
  [[nodiscard]] u16 rangeCentsForSlide(PitchType startPitch,
                                            PitchType targetPitch,
                                            u16 minimumRangeCents) const {
    if (m_pitchToCents) {
      const double startDeviation = std::abs(centsForPitch(startPitch));
      const double targetDeviation = std::abs(centsForPitch(targetPitch));
      return std::max<u16>(
          minimumRangeCents,
          static_cast<u16>(std::ceil(std::max(startDeviation, targetDeviation))));
    }

    const double startDeviation = std::abs(static_cast<double>(startPitch - m_basePitch));
    const double targetDeviation = std::abs(static_cast<double>(targetPitch - m_basePitch));
    return rangeCentsForPitchSpan(std::max(startDeviation, targetDeviation), minimumRangeCents);
  }

  [[nodiscard]] bool atRest(u16 defaultRangeCents) const {
    return m_pitchBendRangeCents == defaultRangeCents && m_currentPitchBend == 0;
  }

  // Convert source-space pitch to MIDI bend using the current bend range.
  [[nodiscard]] s16 bendForPitch(PitchType pitch) const {
    const auto bend = static_cast<s32>(
        std::lround(((centsForPitch(pitch) / m_pitchBendRangeCents) * 8192.0)));
    return static_cast<s16>(std::clamp(bend, kMinMidiPitchBend, kMaxMidiPitchBend));
  }

  [[nodiscard]] s16 bendForCurrentPitch() const {
    return bendForPitch(m_pitch.current());
  }

  // Emit bend when it differs from the cached bend, then update the cache.
  template <typename EmitBend>
  bool setBend(s16 bend, EmitBend&& emitBend) {
    if (bend == m_currentPitchBend) {
      return false;
    }

    std::forward<EmitBend>(emitBend)(bend);
    m_currentPitchBend = bend;
    return true;
  }

  // Emit bend for the current source-space pitch if it changed; invalid bases use center bend.
  template <typename EmitBend>
  bool applyCurrentBend(EmitBend&& emitBend) {
    if (!m_baseValid) {
      return setBend(0, std::forward<EmitBend>(emitBend));
    }

    return setBend(bendForCurrentPitch(), std::forward<EmitBend>(emitBend));
  }

  // If the range changes, emit it and update bend output for the new scaling.
  template <typename EmitRange, typename EmitBend>
  bool setRange(u16 cents, EmitRange&& emitRange, EmitBend&& emitBend) {
    if (cents == 0 || cents == m_pitchBendRangeCents) {
      return false;
    }

    std::forward<EmitRange>(emitRange)(cents);
    m_pitchBendRangeCents = cents;
    if (m_baseValid) {
      applyCurrentBend(std::forward<EmitBend>(emitBend));
    }
    return true;
  }

  // Restore the default range if needed, then emit centered bend if it changed.
  template <typename EmitRange, typename EmitBend>
  void resetRangeAndBend(u16 defaultRangeCents, EmitRange&& emitRange, EmitBend&& emitBend) {
    if (m_pitchBendRangeCents != defaultRangeCents) {
      std::forward<EmitRange>(emitRange)(defaultRangeCents);
      m_pitchBendRangeCents = defaultRangeCents;
    }
    setBend(0, std::forward<EmitBend>(emitBend));
  }

  // Cancel any previous slide. Valid slides set range and start pitch motion;
  // invalid slides restore the default range.
  template <typename EmitRange, typename EmitBend>
  bool beginSlide(const SeqMotionPlan<PitchType>& motion,
                  u16 rangeCents,
                  u16 defaultRangeCents,
                  bool applyInitialBend,
                  EmitRange&& emitRange,
                  EmitBend&& emitBend) {
    clearMotion();
    if (!m_baseValid || (motion.usesTicks() && motion.ticks == 0)) {
      setRange(defaultRangeCents, emitRange, emitBend);
      return false;
    }

    setRange(rangeCents, emitRange, emitBend);
    m_pitch.begin(motion);
    if (applyInitialBend) {
      applyCurrentBend(emitBend);
    }
    return true;
  }

  // Advance pitch motion only when a base note is valid. This does not emit bend.
  SeqMotionTick<PitchType> tick() {
    if (!m_baseValid) {
      return {};
    }
    return m_pitch.tick();
  }

  // Advance pitch motion. On running or finished ticks, emit bend if it changed.
  // Delayed ticks do not emit bend.
  template <typename EmitBend>
  SeqMotionTick<PitchType> tickBend(EmitBend&& emitBend) {
    const auto motionTick = tick();
    if (motionTick.status != SeqMotionStatus::Inactive &&
        motionTick.status != SeqMotionStatus::Delayed) {
      applyCurrentBend(std::forward<EmitBend>(emitBend));
    }
    return motionTick;
  }

 private:
  static constexpr s32 kMinMidiPitchBend = -8192;
  static constexpr s32 kMaxMidiPitchBend = 8191;

  [[nodiscard]] double centsForPitch(PitchType pitch) const {
    if (m_pitchToCents) {
      return m_pitchToCents(pitch, m_basePitch);
    }

    return static_cast<double>(pitch - m_basePitch) * m_centsPerPitchUnit;
  }

  bool m_baseValid = false;
  PitchType m_basePitch {};
  SeqAutomatedValue<PitchType> m_pitch;
  u16 m_pitchBendRangeCents = 200;
  s16 m_currentPitchBend = 0;
  double m_centsPerPitchUnit = 100.0;
  PitchToCents m_pitchToCents {};
};

// Manages synth LFO state: stored delay/rate/depth and optional per-note fade-ins.
// It also caches the last MIDI depth emitted, so repeated values can be skipped.
class SeqSynthLfoAutomation {
 public:
  void reset() {
    clearConfig();
    m_midiDepth.reset(0);
  }

  // Set driver delay, rate, and depth; clear any fade template from the previous config.
  void configure(u8 delay, u8 rate, u8 depth) {
    m_delay = delay;
    m_rate = rate;
    m_depth = depth;
    clearReusableFade();
  }

  void clearConfig() {
    m_delay = 0;
    m_rate = 0;
    m_depth = 0;
    clearReusableFade();
    m_fade.reset();
  }

  [[nodiscard]] bool active() const { return m_rate != 0 && m_depth != 0; }
  [[nodiscard]] u8 delay() const { return m_delay; }
  [[nodiscard]] u8 rate() const { return m_rate; }
  [[nodiscard]] u8 depth() const { return m_depth; }
  void setRate(u8 rate) { m_rate = rate; }
  void setDepth(u8 depth) { m_depth = depth; }

  [[nodiscard]] s32 configuredDepth(u8 fractionalBits = 0) const {
    return static_cast<s32>(m_depth) << fractionalBits;
  }

  // Return stored depth when output is enabled, otherwise zero. Stored depth is unchanged.
  [[nodiscard]] u8 outputDepthWhen(bool enabled) const {
    return enabled ? m_depth : 0;
  }

  // Clamp fade depth so it cannot exceed the configured driver depth.
  [[nodiscard]] s32 clampToConfiguredDepth(s32 depth, u8 fractionalBits = 0) const {
    return std::min(configuredDepth(fractionalBits), depth);
  }

  // Clear the future-note fade template and any active fade using it.
  void clearReusableFade() {
    m_reusableFade.clear();
    m_fade.clearMotion();
  }

  void setReusableFade(u32 ticks, s32 step) {
    m_reusableFade.set(ticks, step);
  }

  void setReusableFadeToTarget(u32 ticks, s32 targetDepth) {
    m_reusableFade.setToTarget(ticks, targetDepth);
  }

  // Store a fade template that moves from zero to the configured LFO depth.
  void setReusableFadeToConfiguredDepth(u32 ticks, u8 fractionalBits = 0) {
    setReusableFadeToTarget(ticks, configuredDepth(fractionalBits));
  }

  [[nodiscard]] bool hasReusableFade() const { return m_reusableFade.active(); }
  [[nodiscard]] u32 reusableFadeLength() const { return m_reusableFade.ticks(); }
  [[nodiscard]] s32 reusableFadeStep() const { return m_reusableFade.step(); }

  // Start the stored fade template for this note; clear active fade if no template exists.
  void beginReusableFade(u32 delay, s32 targetDepth, s32 initialDepth = 0) {
    if (!hasReusableFade()) {
      m_fade.clearMotion();
      return;
    }

    m_fade.setCurrent(initialDepth);
    m_fade.begin(m_reusableFade.instantiate(targetDepth, delay));
  }

  // Start the stored fade template toward the configured LFO depth.
  bool beginReusableFadeToConfiguredDepth(u8 fractionalBits = 0, s32 initialDepth = 0) {
    if (!hasReusableFade()) {
      return false;
    }

    beginReusableFade(m_delay, configuredDepth(fractionalBits), initialDepth);
    return true;
  }

  [[nodiscard]] bool fadeActive() const { return m_fade.active(); }
  // Replace current fade depth without cancelling the active fade.
  void setCurrentDepthPreservingMotion(s32 depth) {
    m_fade.setCurrentPreservingMotion(depth);
  }
  [[nodiscard]] s32 currentDepth() const { return m_fade.current(); }
  [[nodiscard]] u8 midiDepth() const { return m_midiDepth.current(); }
  // Update the cached MIDI depth without emitting output.
  void setMidiDepth(u8 depth) { m_midiDepth.setCachedValue(depth); }

  // Emit depth when it differs from the cached MIDI depth, or when force is true.
  template <typename EmitDepth>
  bool emitDepth(u8 depth, EmitDepth&& emitDepth, bool force = false) {
    return m_midiDepth.emitIfChanged(depth, std::forward<EmitDepth>(emitDepth), force);
  }

  // Advance active fade one tick. On running or finished ticks, clamp the depth,
  // convert it to MIDI, and emit it if it changed.
  template <typename ConvertDepth, typename EmitDepth>
  SeqMotionTick<s32> tickFadeToDepth(u8 fractionalBits,
                                         ConvertDepth&& convertDepth,
                                         EmitDepth&& emitValue) {
    if (!fadeActive()) {
      return {};
    }

    const auto motionTick = m_fade.tick();
    if (motionTick.status != SeqMotionStatus::Inactive &&
        motionTick.status != SeqMotionStatus::Delayed) {
      const s32 current = clampToConfiguredDepth(motionTick.current, fractionalBits);
      setCurrentDepthPreservingMotion(current);
      const int midiDepth = static_cast<int>(std::forward<ConvertDepth>(convertDepth)(current));
      emitDepth(static_cast<u8>(std::clamp(midiDepth, 0, 127)),
                std::forward<EmitDepth>(emitValue));
    }
    return motionTick;
  }

 private:
  u8 m_delay = 0;
  u8 m_rate = 0;
  u8 m_depth = 0;
  SeqAutomatedValue<s32> m_fade;
  SeqMotionPreset<s32> m_reusableFade;
  SeqCachedEmitter<u8> m_midiDepth;
};
