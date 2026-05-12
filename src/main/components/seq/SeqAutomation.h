/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

enum class SeqMotionStep {
  Inactive,
  Delayed,
  Running,
  Finished,
};

template <typename ValueType>
class SeqLinearMotion {
 public:
  void reset(ValueType current = {}) {
    m_current = current;
    m_target = current;
    m_delta = {};
    m_delay = 0;
    m_ticksRemaining = 0;
    m_useLength = true;
  }

  void setCurrent(ValueType current) {
    m_current = current;
    clear();
  }

  void setCurrentValue(ValueType current) {
    m_current = current;
  }

  void clear() {
    m_target = m_current;
    m_delta = {};
    m_delay = 0;
    m_ticksRemaining = 0;
    m_useLength = true;
  }

  void startToTarget(ValueType target, ValueType delta, uint32_t length, uint32_t delay = 0) {
    m_target = target;
    m_delta = delta;
    m_delay = delay;
    m_ticksRemaining = length;
    m_useLength = true;
  }

  void startByStep(ValueType target, ValueType delta, uint32_t delay = 0) {
    m_target = target;
    m_delta = delta;
    m_delay = delay;
    m_ticksRemaining = 0;
    m_useLength = false;
  }

  [[nodiscard]] bool active() const {
    return m_delay != 0 || (m_useLength ? m_ticksRemaining != 0 : m_delta != ValueType {});
  }

  [[nodiscard]] ValueType current() const { return m_current; }
  [[nodiscard]] ValueType target() const { return m_target; }
  [[nodiscard]] ValueType delta() const { return m_delta; }
  [[nodiscard]] uint32_t delayRemaining() const { return m_delay; }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_ticksRemaining; }
  [[nodiscard]] bool usesLength() const { return m_useLength; }

  SeqMotionStep advance() {
    if (m_delay != 0) {
      m_delay -= 1;
      return SeqMotionStep::Delayed;
    }

    if (m_useLength) {
      if (m_ticksRemaining == 0) {
        return SeqMotionStep::Inactive;
      }

      m_ticksRemaining -= 1;
      if (m_ticksRemaining == 0) {
        m_current = m_target;
        return SeqMotionStep::Finished;
      }

      m_current = static_cast<ValueType>(m_current + m_delta);
      return SeqMotionStep::Running;
    }

    if (m_delta == ValueType {}) {
      return SeqMotionStep::Inactive;
    }

    m_current = static_cast<ValueType>(m_current + m_delta);
    if ((m_delta > ValueType {} && m_current >= m_target) ||
        (m_delta < ValueType {} && m_current <= m_target)) {
      m_current = m_target;
      m_delta = {};
      return SeqMotionStep::Finished;
    }

    return SeqMotionStep::Running;
  }

 private:
  ValueType m_current {};
  ValueType m_target {};
  ValueType m_delta {};
  uint32_t m_delay = 0;
  uint32_t m_ticksRemaining = 0;
  bool m_useLength = true;
};

template <typename ValueType>
class ControllerLane {
 public:
  void reset(ValueType current = {}) { m_motion.reset(current); }
  void setCurrent(ValueType current) { m_motion.setCurrent(current); }
  void setCurrentValue(ValueType current) { m_motion.setCurrentValue(current); }
  void clear() { m_motion.clear(); }

  void startToTarget(ValueType target, ValueType delta, uint32_t length, uint32_t delay = 0) {
    m_motion.startToTarget(target, delta, length, delay);
  }

  void startByStep(ValueType target, ValueType delta, uint32_t delay = 0) {
    m_motion.startByStep(target, delta, delay);
  }

  [[nodiscard]] bool active() const { return m_motion.active(); }
  [[nodiscard]] ValueType current() const { return m_motion.current(); }
  [[nodiscard]] ValueType target() const { return m_motion.target(); }
  [[nodiscard]] ValueType delta() const { return m_motion.delta(); }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_motion.ticksRemaining(); }
  [[nodiscard]] bool usesLength() const { return m_motion.usesLength(); }

  SeqMotionStep advance() { return m_motion.advance(); }

  template <typename Apply>
  SeqMotionStep advanceAndApply(Apply&& apply, bool applyDelayedStep = false) {
    const SeqMotionStep step = advance();
    if (step != SeqMotionStep::Inactive &&
        (applyDelayedStep || step != SeqMotionStep::Delayed)) {
      std::forward<Apply>(apply)(m_motion.current());
    }
    return step;
  }

 private:
  SeqLinearMotion<ValueType> m_motion;
};

template <typename ValueType = int32_t, unsigned FractionBits = 8>
class FixedPointControllerLane {
 public:
  static constexpr ValueType kScale = static_cast<ValueType>(1) << FractionBits;

  static constexpr ValueType toFixed(ValueType value) {
    return value << FractionBits;
  }

  static constexpr ValueType toRaw(ValueType value) {
    return value >> FractionBits;
  }

  void reset(ValueType currentRaw = {}) { m_lane.reset(toFixed(currentRaw)); }
  void setCurrent(ValueType currentRaw) { m_lane.setCurrent(toFixed(currentRaw)); }
  void setCurrentFixed(ValueType currentFixed) { m_lane.setCurrent(currentFixed); }
  void clear() { m_lane.clear(); }

  [[nodiscard]] bool active() const { return m_lane.active(); }
  [[nodiscard]] ValueType currentFixed() const { return m_lane.current(); }
  [[nodiscard]] ValueType currentRaw() const { return toRaw(m_lane.current()); }
  [[nodiscard]] ValueType targetFixed() const { return m_lane.target(); }
  [[nodiscard]] ValueType targetRaw() const { return toRaw(m_lane.target()); }
  [[nodiscard]] ValueType delta() const { return m_lane.delta(); }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_lane.ticksRemaining(); }
  [[nodiscard]] bool usesLength() const { return m_lane.usesLength(); }

  [[nodiscard]] ValueType currentRawFixed() const {
    return toFixed(currentRaw());
  }

  [[nodiscard]] ValueType deltaToTarget(ValueType targetRaw, uint32_t length) const {
    if (length == 0) {
      return {};
    }
    return static_cast<ValueType>((toFixed(targetRaw) - currentRawFixed()) /
                                  static_cast<ValueType>(length));
  }

  void startToTarget(ValueType targetRaw, uint32_t length, uint32_t delay = 0) {
    m_lane.setCurrent(currentRawFixed());
    m_lane.startToTarget(toFixed(targetRaw), deltaToTarget(targetRaw, length), length, delay);
  }

  void startToTarget(ValueType targetRaw, ValueType delta, uint32_t length, uint32_t delay = 0) {
    m_lane.setCurrent(currentRawFixed());
    m_lane.startToTarget(toFixed(targetRaw), delta, length, delay);
  }

  void startByStep(ValueType targetRaw, ValueType delta, uint32_t delay = 0) {
    m_lane.setCurrent(currentRawFixed());
    m_lane.startByStep(toFixed(targetRaw), delta, delay);
  }

  template <typename Apply>
  bool beginToTarget(ValueType targetRaw, uint32_t length, Apply&& apply) {
    if (length == 0) {
      setCurrent(targetRaw);
      std::forward<Apply>(apply)(currentRaw());
      return true;
    }

    startToTarget(targetRaw, length);
    return false;
  }

  template <typename Apply>
  bool beginByStep(ValueType targetRaw, ValueType delta, Apply&& apply) {
    if (delta == ValueType {}) {
      setCurrent(targetRaw);
      std::forward<Apply>(apply)(currentRaw());
      return true;
    }

    startByStep(targetRaw, delta);
    return false;
  }

  SeqMotionStep advance() { return m_lane.advance(); }

  template <typename Apply>
  SeqMotionStep advanceAndApply(Apply&& apply, bool applyDelayedStep = false) {
    return m_lane.advanceAndApply(std::forward<Apply>(apply), applyDelayedStep);
  }

  template <typename Apply>
  SeqMotionStep advanceAndApplyRaw(Apply&& apply, bool applyDelayedStep = false) {
    return advanceAndApply(
        [apply = std::forward<Apply>(apply)](ValueType currentFixed) mutable {
          apply(toRaw(currentFixed));
        },
        applyDelayedStep);
  }

 private:
  ControllerLane<ValueType> m_lane;
};

template <typename PitchType>
struct PitchMotionSpec {
  PitchType target {};
  PitchType delta {};
  uint32_t length = 0;
  uint32_t delay = 0;
};

template <typename PitchType>
class PitchBendLane {
 public:
  explicit PitchBendLane(double centsPerPitchUnit = 100.0)
      : m_centsPerPitchUnit(centsPerPitchUnit) {}

  void reset(uint16_t defaultRangeCents) {
    m_baseValid = false;
    m_basePitch = {};
    m_motion.reset();
    m_pitchBendRangeCents = defaultRangeCents;
    m_currentPitchBend = 0;
  }

  void setCentsPerPitchUnit(double centsPerPitchUnit) {
    m_centsPerPitchUnit = centsPerPitchUnit;
  }

  void beginNote(PitchType basePitch) {
    m_baseValid = true;
    m_basePitch = basePitch;
    m_motion.setCurrent(basePitch);
  }

  void invalidateBase() { m_baseValid = false; }
  [[nodiscard]] bool baseValid() const { return m_baseValid; }

  void clearMotion() { m_motion.clear(); }

  void setCurrentPitch(PitchType pitch) {
    m_motion.setCurrent(pitch);
  }

  [[nodiscard]] PitchType deltaToTarget(PitchType targetPitch, uint32_t length) const {
    if (length == 0) {
      return {};
    }
    return static_cast<PitchType>((targetPitch - currentPitch()) / static_cast<PitchType>(length));
  }

  [[nodiscard]] PitchMotionSpec<PitchType> motionToTarget(PitchType targetPitch,
                                                          uint32_t length,
                                                          uint32_t delay = 0) const {
    return {targetPitch, deltaToTarget(targetPitch, length), length, delay};
  }

  bool startMotion(PitchType targetPitch, PitchType delta, uint32_t length, uint32_t delay = 0) {
    if (!m_baseValid || length == 0) {
      return false;
    }
    m_motion.startToTarget(targetPitch, delta, length, delay);
    return true;
  }

  bool startMotion(const PitchMotionSpec<PitchType>& motion) {
    return startMotion(motion.target, motion.delta, motion.length, motion.delay);
  }

  SeqMotionStep advanceMotion() {
    if (!m_baseValid) {
      return SeqMotionStep::Inactive;
    }
    return m_motion.advance();
  }

  template <typename EmitBend>
  SeqMotionStep advanceAndApplyBend(EmitBend&& emitBend) {
    const SeqMotionStep step = advanceMotion();
    if (step != SeqMotionStep::Inactive && step != SeqMotionStep::Delayed) {
      applyCurrentBend(std::forward<EmitBend>(emitBend));
    }
    return step;
  }

  [[nodiscard]] PitchType basePitch() const { return m_basePitch; }
  [[nodiscard]] PitchType currentPitch() const { return m_motion.current(); }
  [[nodiscard]] bool motionActive() const { return m_motion.active(); }
  [[nodiscard]] uint32_t motionTicksRemaining() const { return m_motion.ticksRemaining(); }

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
                      m_pitchBendRangeCents) *
                     8192.0)));
    return static_cast<int16_t>(std::clamp(bend, kMinMidiPitchBend, kMaxMidiPitchBend));
  }

  [[nodiscard]] int16_t bendForCurrentPitch() const {
    return bendForPitch(m_motion.current());
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

 private:
  static constexpr int32_t kMinMidiPitchBend = -8192;
  static constexpr int32_t kMaxMidiPitchBend = 8191;

  bool m_baseValid = false;
  PitchType m_basePitch {};
  SeqLinearMotion<PitchType> m_motion;
  uint16_t m_pitchBendRangeCents = 200;
  int16_t m_currentPitchBend = 0;
  double m_centsPerPitchUnit = 100.0;
};

class SynthLfoLane {
 public:
  void reset() {
    m_delay = 0;
    m_rate = 0;
    m_depth = 0;
    m_fade.reset();
    m_fadeLength = 0;
    m_fadeStep = 0;
    m_midiDepth = 0;
  }

  void configure(uint8_t delay, uint8_t rate, uint8_t depth) {
    m_delay = delay;
    m_rate = rate;
    m_depth = depth;
  }

  void clearConfig() {
    m_delay = 0;
    m_rate = 0;
    m_depth = 0;
    clearReusableFade();
    setCurrentDepth(0);
    m_midiDepth = 0;
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

  [[nodiscard]] int32_t clampToConfiguredDepth(int32_t depth, uint8_t fractionalBits = 0) const {
    return std::min(configuredDepth(fractionalBits), depth);
  }

  [[nodiscard]] uint8_t outputDepthWhen(bool enabled) const {
    return enabled ? m_depth : 0;
  }

  void clearReusableFade() {
    m_fadeLength = 0;
    m_fadeStep = 0;
    m_fade.clear();
  }

  void setReusableFade(uint32_t length, int32_t step) {
    m_fadeLength = length;
    m_fadeStep = step;
  }

  void setReusableFadeToTarget(uint32_t length, int32_t targetDepth) {
    setReusableFade(length, length == 0 ? 0 : targetDepth / static_cast<int32_t>(length));
  }

  void setReusableFadeToConfiguredDepth(uint32_t length, uint8_t fractionalBits = 0) {
    setReusableFadeToTarget(length, configuredDepth(fractionalBits));
  }

  [[nodiscard]] bool hasReusableFade() const { return m_fadeLength != 0; }
  [[nodiscard]] uint32_t reusableFadeLength() const { return m_fadeLength; }
  [[nodiscard]] int32_t reusableFadeStep() const { return m_fadeStep; }

  void startReusableFade(uint32_t delay, int32_t targetDepth, int32_t initialDepth = 0) {
    if (m_fadeLength == 0) {
      m_fade.clear();
      return;
    }
    m_fade.setCurrent(initialDepth);
    m_fade.startToTarget(targetDepth, m_fadeStep, m_fadeLength, delay);
  }

  void startReusableFadeToConfiguredDepth(uint8_t fractionalBits = 0, int32_t initialDepth = 0) {
    startReusableFade(m_delay, configuredDepth(fractionalBits), initialDepth);
  }

  SeqMotionStep advanceFade() { return m_fade.advance(); }
  [[nodiscard]] bool fadeActive() const { return m_fade.active(); }

  void setCurrentDepth(int32_t depth) {
    m_fade.setCurrentValue(depth);
  }

  [[nodiscard]] int32_t currentDepth() const { return m_fade.current(); }
  [[nodiscard]] uint8_t midiDepth() const { return m_midiDepth; }
  void setMidiDepth(uint8_t depth) { m_midiDepth = depth; }

  template <typename EmitDepth>
  bool setOutputDepth(uint8_t depth, EmitDepth&& emitDepth, bool force = false) {
    if (!force && depth == m_midiDepth) {
      return false;
    }

    std::forward<EmitDepth>(emitDepth)(depth);
    m_midiDepth = depth;
    return true;
  }

  template <typename ApplyDepth>
  SeqMotionStep advanceFadeAndApply(ApplyDepth&& applyDepth) {
    if (!fadeActive()) {
      return SeqMotionStep::Inactive;
    }

    const SeqMotionStep step = advanceFade();
    if (step != SeqMotionStep::Inactive && step != SeqMotionStep::Delayed) {
      std::forward<ApplyDepth>(applyDepth)(currentDepth());
    }
    return step;
  }

  template <typename ApplyDepth>
  SeqMotionStep advanceFadeAndApplyClamped(uint8_t fractionalBits, ApplyDepth&& applyDepth) {
    return advanceFadeAndApply(
        [this, fractionalBits, applyDepth = std::forward<ApplyDepth>(applyDepth)](int32_t depth) mutable {
          applyDepth(clampToConfiguredDepth(depth, fractionalBits));
        });
  }

 private:
  uint8_t m_delay = 0;
  uint8_t m_rate = 0;
  uint8_t m_depth = 0;
  SeqLinearMotion<int32_t> m_fade;
  uint32_t m_fadeLength = 0;
  int32_t m_fadeStep = 0;
  uint8_t m_midiDepth = 0;
};
