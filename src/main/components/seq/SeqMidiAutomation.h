/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "SeqAutomation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

template <typename PitchType>
class SeqPitchBendAutomation {
 public:
  explicit SeqPitchBendAutomation(double centsPerPitchUnit = 100.0)
      : m_centsPerPitchUnit(centsPerPitchUnit) {}

  void reset(uint16_t defaultRangeCents) {
    m_baseValid = false;
    m_basePitch = {};
    m_pitch.reset();
    m_pitchBendRangeCents = defaultRangeCents;
    m_currentPitchBend = 0;
  }

  void setCentsPerPitchUnit(double centsPerPitchUnit) {
    m_centsPerPitchUnit = centsPerPitchUnit;
  }

  void beginNote(PitchType basePitch) {
    m_baseValid = true;
    m_basePitch = basePitch;
    m_pitch.jumpTo(basePitch);
  }

  void invalidateBase() { m_baseValid = false; }
  [[nodiscard]] bool baseValid() const { return m_baseValid; }

  void clearMotion() { m_pitch.clearMotion(); }
  void jumpToPitch(PitchType pitch) { m_pitch.jumpTo(pitch); }
  void setCurrentPitchPreservingMotion(PitchType pitch) {
    m_pitch.setCurrentPreservingMotion(pitch);
  }

  [[nodiscard]] PitchType basePitch() const { return m_basePitch; }
  [[nodiscard]] PitchType currentPitch() const { return m_pitch.current(); }
  [[nodiscard]] bool motionActive() const { return m_pitch.active(); }
  [[nodiscard]] uint32_t motionTicksRemaining() const { return m_pitch.ticksRemaining(); }

  [[nodiscard]] PitchType stepToTarget(PitchType targetPitch, uint32_t ticks) const {
    if (ticks == 0) {
      return {};
    }
    return static_cast<PitchType>((targetPitch - currentPitch()) /
                                  static_cast<PitchType>(ticks));
  }

  [[nodiscard]] SeqMotionPlan<PitchType> motionToTarget(PitchType targetPitch,
                                                        uint32_t ticks,
                                                        uint32_t delay = 0) const {
    return SeqMotionPlan<PitchType>::targetOverTicks(targetPitch, ticks, delay);
  }

  [[nodiscard]] uint16_t rangeCentsForPitchSpan(double pitchUnits,
                                                uint16_t minimumRangeCents) const {
    const auto cents = static_cast<uint16_t>(
        std::ceil(std::abs(pitchUnits) * m_centsPerPitchUnit));
    return std::max<uint16_t>(minimumRangeCents, cents);
  }

  [[nodiscard]] uint16_t rangeCentsForSlide(PitchType startPitch,
                                            PitchType targetPitch,
                                            uint16_t minimumRangeCents) const {
    const double startDeviation = std::abs(static_cast<double>(startPitch - m_basePitch));
    const double targetDeviation = std::abs(static_cast<double>(targetPitch - m_basePitch));
    return rangeCentsForPitchSpan(std::max(startDeviation, targetDeviation), minimumRangeCents);
  }

  [[nodiscard]] bool atRest(uint16_t defaultRangeCents) const {
    return m_pitchBendRangeCents == defaultRangeCents && m_currentPitchBend == 0;
  }

  [[nodiscard]] int16_t bendForPitch(PitchType pitch) const {
    const auto bend = static_cast<int32_t>(
        std::lround((((static_cast<double>(pitch - m_basePitch) * m_centsPerPitchUnit) /
                      m_pitchBendRangeCents) * 8192.0)));
    return static_cast<int16_t>(std::clamp(bend, kMinMidiPitchBend, kMaxMidiPitchBend));
  }

  [[nodiscard]] int16_t bendForCurrentPitch() const {
    return bendForPitch(m_pitch.current());
  }

  template <typename EmitBend>
  bool setBend(int16_t bend, EmitBend&& emitBend) {
    if (bend == m_currentPitchBend) {
      return false;
    }

    std::forward<EmitBend>(emitBend)(bend);
    m_currentPitchBend = bend;
    return true;
  }

  template <typename EmitBend>
  bool applyCurrentBend(EmitBend&& emitBend) {
    if (!m_baseValid) {
      return setBend(0, std::forward<EmitBend>(emitBend));
    }

    return setBend(bendForCurrentPitch(), std::forward<EmitBend>(emitBend));
  }

  template <typename EmitRange, typename EmitBend>
  bool setRange(uint16_t cents, EmitRange&& emitRange, EmitBend&& emitBend) {
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

  template <typename EmitRange, typename EmitBend>
  void resetRangeAndBend(uint16_t defaultRangeCents, EmitRange&& emitRange, EmitBend&& emitBend) {
    if (m_pitchBendRangeCents != defaultRangeCents) {
      std::forward<EmitRange>(emitRange)(defaultRangeCents);
      m_pitchBendRangeCents = defaultRangeCents;
    }
    setBend(0, std::forward<EmitBend>(emitBend));
  }

  template <typename EmitRange, typename EmitBend>
  bool beginSlide(const SeqMotionPlan<PitchType>& motion,
                  uint16_t rangeCents,
                  uint16_t defaultRangeCents,
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

  SeqMotionTick<PitchType> tick() {
    if (!m_baseValid) {
      return {};
    }
    return m_pitch.tick();
  }

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
  static constexpr int32_t kMinMidiPitchBend = -8192;
  static constexpr int32_t kMaxMidiPitchBend = 8191;

  bool m_baseValid = false;
  PitchType m_basePitch {};
  SeqAutomatedValue<PitchType> m_pitch;
  uint16_t m_pitchBendRangeCents = 200;
  int16_t m_currentPitchBend = 0;
  double m_centsPerPitchUnit = 100.0;
};

class SeqSynthLfoAutomation {
 public:
  void reset() {
    clearConfig();
    m_midiDepth.reset(0);
  }

  void configure(uint8_t delay, uint8_t rate, uint8_t depth) {
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
  [[nodiscard]] uint8_t delay() const { return m_delay; }
  [[nodiscard]] uint8_t rate() const { return m_rate; }
  [[nodiscard]] uint8_t depth() const { return m_depth; }
  void setRate(uint8_t rate) { m_rate = rate; }
  void setDepth(uint8_t depth) { m_depth = depth; }

  [[nodiscard]] int32_t configuredDepth(uint8_t fractionalBits = 0) const {
    return static_cast<int32_t>(m_depth) << fractionalBits;
  }

  [[nodiscard]] uint8_t outputDepthWhen(bool enabled) const {
    return enabled ? m_depth : 0;
  }

  [[nodiscard]] int32_t clampToConfiguredDepth(int32_t depth, uint8_t fractionalBits = 0) const {
    return std::min(configuredDepth(fractionalBits), depth);
  }

  void clearReusableFade() {
    m_reusableFade.clear();
    m_fade.clearMotion();
  }

  void setReusableFade(uint32_t ticks, int32_t step) {
    m_reusableFade.set(ticks, step);
  }

  void setReusableFadeToTarget(uint32_t ticks, int32_t targetDepth) {
    m_reusableFade.setToTarget(ticks, targetDepth);
  }

  void setReusableFadeToConfiguredDepth(uint32_t ticks, uint8_t fractionalBits = 0) {
    setReusableFadeToTarget(ticks, configuredDepth(fractionalBits));
  }

  [[nodiscard]] bool hasReusableFade() const { return m_reusableFade.active(); }
  [[nodiscard]] uint32_t reusableFadeLength() const { return m_reusableFade.ticks(); }
  [[nodiscard]] int32_t reusableFadeStep() const { return m_reusableFade.step(); }

  void beginReusableFade(uint32_t delay, int32_t targetDepth, int32_t initialDepth = 0) {
    if (!hasReusableFade()) {
      m_fade.clearMotion();
      return;
    }

    m_fade.jumpTo(initialDepth);
    m_fade.begin(m_reusableFade.instantiate(targetDepth, delay));
  }

  bool beginReusableFadeToConfiguredDepth(uint8_t fractionalBits = 0, int32_t initialDepth = 0) {
    if (!hasReusableFade()) {
      return false;
    }

    beginReusableFade(m_delay, configuredDepth(fractionalBits), initialDepth);
    return true;
  }

  [[nodiscard]] bool fadeActive() const { return m_fade.active(); }
  void setCurrentDepthPreservingMotion(int32_t depth) {
    m_fade.setCurrentPreservingMotion(depth);
  }
  [[nodiscard]] int32_t currentDepth() const { return m_fade.current(); }
  [[nodiscard]] uint8_t midiDepth() const { return m_midiDepth.current(); }
  void setMidiDepth(uint8_t depth) { m_midiDepth.setCachedValue(depth); }

  template <typename EmitDepth>
  bool emitDepth(uint8_t depth, EmitDepth&& emitDepth, bool force = false) {
    return m_midiDepth.emitIfChanged(depth, std::forward<EmitDepth>(emitDepth), force);
  }

  template <typename ConvertDepth, typename EmitDepth>
  SeqMotionTick<int32_t> tickFadeToDepth(uint8_t fractionalBits,
                                         ConvertDepth&& convertDepth,
                                         EmitDepth&& emitValue) {
    if (!fadeActive()) {
      return {};
    }

    const auto motionTick = m_fade.tick();
    if (motionTick.status != SeqMotionStatus::Inactive &&
        motionTick.status != SeqMotionStatus::Delayed) {
      const int32_t current = clampToConfiguredDepth(motionTick.current, fractionalBits);
      setCurrentDepthPreservingMotion(current);
      const int midiDepth = static_cast<int>(std::forward<ConvertDepth>(convertDepth)(current));
      emitDepth(static_cast<uint8_t>(std::clamp(midiDepth, 0, 127)),
                std::forward<EmitDepth>(emitValue));
    }
    return motionTick;
  }

 private:
  uint8_t m_delay = 0;
  uint8_t m_rate = 0;
  uint8_t m_depth = 0;
  SeqAutomatedValue<int32_t> m_fade;
  SeqMotionPreset<int32_t> m_reusableFade;
  SeqCachedEmitter<uint8_t> m_midiDepth;
};
