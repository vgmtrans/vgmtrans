/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "SeqMotion.h"
#include <algorithm>
#include <cstdint>
#include <utility>

// Wraps SeqLinearMotion with apply callbacks. This layer is still MIDI-agnostic;
// callers decide what applying a value means.
template <typename ValueType>
class SeqAutomatedValue {
 public:
  void reset(ValueType current = {}) { m_motion.reset(current); }
  void setCurrent(ValueType current) { m_motion.setCurrent(current); }
  // Replace current value without clearing the active motion.
  void setCurrentPreservingMotion(ValueType current) {
    m_motion.setCurrentPreservingMotion(current);
  }
  void clearMotion() { m_motion.clear(); }

  [[nodiscard]] bool active() const { return m_motion.active(); }
  [[nodiscard]] ValueType current() const { return m_motion.current(); }
  [[nodiscard]] ValueType target() const { return m_motion.target(); }
  [[nodiscard]] ValueType step() const { return m_motion.step(); }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_motion.ticksRemaining(); }
  [[nodiscard]] bool usesTicks() const { return m_motion.usesTicks(); }

  SeqMotionTick<ValueType> begin(const SeqMotionPlan<ValueType>& motion) {
    return m_motion.begin(motion);
  }

  template <typename Apply>
  SeqMotionTick<ValueType> begin(const SeqMotionPlan<ValueType>& motion, Apply&& apply) {
    const auto tick = begin(motion);
    // Call Apply only if begin() changes the value and finishes immediately.
    if (tick.status == SeqMotionStatus::Finished && tick.changed) {
      std::forward<Apply>(apply)(tick.current);
    }
    return tick;
  }

  // Advance one driver tick and return its status and values.
  // This updates state only; it does not call an apply callback or emit output.
  SeqMotionTick<ValueType> tick() { return m_motion.tick(); }

  // Advance one tick. Then call Apply on active non-delayed ticks.
  // This can call Apply even when the value did not change.
  template <typename Apply>
  SeqMotionTick<ValueType> tick(Apply&& apply, bool applyDelayedStep = false) {
    const auto motionTick = tick();
    if (motionTick.shouldApply(applyDelayedStep)) {
      std::forward<Apply>(apply)(motionTick.current);
    }
    return motionTick;
  }

  // Advance one tick. Then call Apply only on active non-delayed ticks whose value changed.
  template <typename Apply>
  SeqMotionTick<ValueType> tickChanged(Apply&& apply, bool applyDelayedStep = false) {
    const auto motionTick = tick();
    if (motionTick.shouldApply(applyDelayedStep) && motionTick.changed) {
      std::forward<Apply>(apply)(motionTick.current);
    }
    return motionTick;
  }

 private:
  SeqLinearMotion<ValueType> m_motion;
};

// Stores the last value passed to emitIfChanged(). When a new value differs,
// emitIfChanged() calls the caller-provided callback and updates the cache.
template <typename ValueType>
class SeqCachedEmitter {
 public:
  void reset(ValueType current = {}) { m_current = current; }
  [[nodiscard]] ValueType current() const { return m_current; }
  void setCachedValue(ValueType current) { m_current = current; }

  // Call emitValue(value) when value differs from the cache, or when force is true.
  template <typename Emit>
  bool emitIfChanged(ValueType value, Emit&& emitValue, bool force = false) {
    if (!force && value == m_current) {
      return false;
    }

    std::forward<Emit>(emitValue)(value);
    m_current = value;
    return true;
  }

 private:
  ValueType m_current {};
};

// Stores a reusable per-tick step and duration for motions started repeatedly.
template <typename ValueType>
class SeqMotionPreset {
 public:
  void clear() {
    m_ticks = 0;
    m_step = {};
  }

  void set(uint32_t ticks, ValueType step) {
    m_ticks = ticks;
    m_step = step;
  }

  // Set the preset step needed to move from zero to targetFromZero.
  void setToTarget(uint32_t ticks, ValueType targetFromZero) {
    set(ticks, ticks == 0 ? ValueType {} : static_cast<ValueType>(targetFromZero /
                                                                  static_cast<ValueType>(ticks)));
  }

  [[nodiscard]] bool active() const { return m_ticks != 0; }
  [[nodiscard]] uint32_t ticks() const { return m_ticks; }
  [[nodiscard]] ValueType step() const { return m_step; }

  // Create a motion using the stored step and ticks; completion snaps to target.
  [[nodiscard]] SeqMotionPlan<ValueType> instantiate(ValueType target, uint32_t delay = 0) const {
    return SeqMotionPlan<ValueType>::targetOverTicksWithStep(target, m_step, m_ticks, delay);
  }

 private:
  uint32_t m_ticks = 0;
  ValueType m_step {};
};

template <typename ValueType, unsigned FractionBits = 8>
struct SeqFixedPointMotionPlan {
  // Targets are raw controller values; steps are fixed-point deltas.
  ValueType targetRaw {};
  ValueType stepFixed {};
  uint32_t ticks = 0;
  uint32_t delay = 0;
  SeqMotionMode mode = SeqMotionMode::TargetOverTicks;

  // Move to a raw target over tickCount ticks; begin() computes the fixed-point step.
  static SeqFixedPointMotionPlan targetRawOverTicks(ValueType targetValue,
                                                    uint32_t tickCount,
                                                    uint32_t delayTicks = 0) {
    return {targetValue, {}, tickCount, delayTicks, SeqMotionMode::TargetOverTicks};
  }

  // Use the supplied fixed-point step for tickCount ticks, then snap to the raw target.
  static SeqFixedPointMotionPlan targetRawOverTicksWithStepFixed(ValueType targetValue,
                                                                 ValueType stepValue,
                                                                 uint32_t tickCount,
                                                                 uint32_t delayTicks = 0) {
    return {targetValue,
            stepValue,
            tickCount,
            delayTicks,
            SeqMotionMode::TargetOverTicksWithStep};
  }

  // Use the supplied fixed-point step as-is until the raw target is reached or crossed.
  static SeqFixedPointMotionPlan targetRawByStepFixed(
      ValueType targetValue,
      ValueType stepValue,
      uint32_t delayTicks = 0) {
    return {targetValue,
            stepValue,
            0,
            delayTicks,
            SeqMotionMode::TargetByStep};
  }

  [[nodiscard]] bool usesTicks() const {
    return mode != SeqMotionMode::TargetByStep;
  }

  [[nodiscard]] bool usesStepUntilTarget() const {
    return mode == SeqMotionMode::TargetByStep;
  }
};

enum class SeqFixedPointRetarget {
  // Start new motions from currentRaw() converted back to fixed point; discard the fraction.
  QuantizeToRaw,
  // Start new motions from the current fixed accumulator; keep the fraction.
  PreserveAccumulator,
};

enum class SeqFixedPointRounding {
  Floor,
  TowardZero,
  Nearest,
};

struct SeqFixedPointPolicy {
  SeqFixedPointRetarget retarget = SeqFixedPointRetarget::QuantizeToRaw;
  SeqFixedPointRounding rounding = SeqFixedPointRounding::Floor;
};

template <typename ValueType, unsigned FractionBits = 8>
struct SeqFixedPointMotion {
  using Plan = SeqFixedPointMotionPlan<ValueType, FractionBits>;

  static Plan toRawTarget(ValueType targetRaw,
                          uint32_t ticks,
                          uint32_t delay = 0) {
    return Plan::targetRawOverTicks(targetRaw, ticks, delay);
  }

  static Plan toRawTargetByFixedStep(
      ValueType targetRaw,
      ValueType stepFixed,
      uint32_t delay = 0) {
    return Plan::targetRawByStepFixed(targetRaw, stepFixed, delay);
  }
};

template <typename ValueType = int32_t, unsigned FractionBits = 8>
class SeqFixedPointAutomation {
 public:
  static constexpr ValueType kScale = static_cast<ValueType>(1) << FractionBits;

  explicit SeqFixedPointAutomation(SeqFixedPointPolicy policy = {}) : m_policy(policy) {}

  static constexpr ValueType toFixed(ValueType rawValue) {
    return rawValue * kScale;
  }

  static constexpr ValueType toRaw(ValueType fixedValue) {
    return fixedToRaw(fixedValue, SeqFixedPointRounding::Floor);
  }

  void setPolicy(SeqFixedPointPolicy policy) { m_policy = policy; }
  [[nodiscard]] SeqFixedPointPolicy policy() const { return m_policy; }

  void reset(ValueType rawCurrent = {}) { m_value.reset(toFixed(rawCurrent)); }
  void setCurrentRaw(ValueType rawCurrent) { m_value.setCurrent(toFixed(rawCurrent)); }
  // Replace the fixed accumulator without clearing the active motion.
  void setCurrentFixedPreservingMotion(ValueType fixedCurrent) {
    m_value.setCurrentPreservingMotion(fixedCurrent);
  }
  void clearMotion() { m_value.clearMotion(); }

  [[nodiscard]] bool active() const { return m_value.active(); }
  [[nodiscard]] ValueType currentFixed() const { return m_value.current(); }
  [[nodiscard]] ValueType currentRaw() const { return rawFromFixed(m_value.current()); }
  [[nodiscard]] ValueType targetFixed() const { return m_value.target(); }
  [[nodiscard]] ValueType targetRaw() const { return rawFromFixed(m_value.target()); }
  [[nodiscard]] ValueType step() const { return m_value.step(); }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_value.ticksRemaining(); }
  [[nodiscard]] bool usesTicks() const { return m_value.usesTicks(); }

  // Return currentRaw() converted back to fixed point, discarding any fraction.
  [[nodiscard]] ValueType quantizedCurrentFixed() const {
    return toFixed(currentRaw());
  }

  // Return the fixed current value selected by the retarget policy for a new motion.
  [[nodiscard]] ValueType currentFixedForNewMotion() const {
    return m_policy.retarget == SeqFixedPointRetarget::PreserveAccumulator
        ? currentFixed()
        : quantizedCurrentFixed();
  }

  // Convert a fixed-point value to raw units using this automation's rounding policy.
  [[nodiscard]] ValueType rawFromFixed(ValueType fixedValue) const {
    return fixedToRaw(fixedValue, m_policy.rounding);
  }

  // Compute the fixed-point step from currentFixedForNewMotion() to targetRaw.
  [[nodiscard]] ValueType stepFixedToTargetRaw(ValueType targetRaw, uint32_t ticks) const {
    if (ticks == 0) {
      return {};
    }
    return static_cast<ValueType>((toFixed(targetRaw) - currentFixedForNewMotion()) /
                                  static_cast<ValueType>(ticks));
  }

  // Convert the raw-facing plan to fixed point and start the resulting motion.
  SeqMotionTick<ValueType> begin(const SeqFixedPointMotionPlan<ValueType, FractionBits>& rawMotion) {
    m_value.setCurrentPreservingMotion(currentFixedForNewMotion());

    SeqMotionPlan<ValueType> fixedMotion {
        toFixed(rawMotion.targetRaw),
        rawMotion.stepFixed,
        rawMotion.ticks,
        rawMotion.delay,
        rawMotion.mode,
    };
    if (rawMotion.mode == SeqMotionMode::TargetOverTicks) {
      fixedMotion.mode = SeqMotionMode::TargetOverTicksWithStep;
      fixedMotion.step = stepFixedToTargetRaw(rawMotion.targetRaw, rawMotion.ticks);
    }

    return m_value.begin(fixedMotion);
  }

  template <typename ApplyRaw>
  SeqMotionTick<ValueType> begin(const SeqFixedPointMotionPlan<ValueType, FractionBits>& rawMotion,
                                 ApplyRaw&& applyRaw) {
    const ValueType previousRaw = currentRaw();
    const auto motionTick = begin(rawMotion);
    const ValueType nextRaw = currentRaw();
    // If begin() finishes immediately, call ApplyRaw only when currentRaw() changed.
    if (motionTick.status == SeqMotionStatus::Finished && nextRaw != previousRaw) {
      std::forward<ApplyRaw>(applyRaw)(nextRaw);
    }
    return motionTick;
  }

  // Advance the fixed-point accumulator one tick without applying raw output.
  SeqMotionTick<ValueType> tick() { return m_value.tick(); }

  // Advance one tick. After advancing, call ApplyRaw only if currentRaw() changed.
  template <typename ApplyRaw>
  SeqMotionTick<ValueType> tickRaw(ApplyRaw&& applyRaw, bool applyDelayedStep = false) {
    const ValueType previousRaw = currentRaw();
    const auto motionTick = tick();
    if (motionTick.shouldApply(applyDelayedStep)) {
      const ValueType nextRaw = currentRaw();
      if (nextRaw != previousRaw) {
        std::forward<ApplyRaw>(applyRaw)(nextRaw);
      }
    }
    return motionTick;
  }

 private:
  static constexpr ValueType fixedToRaw(ValueType fixedValue, SeqFixedPointRounding rounding) {
    if (rounding == SeqFixedPointRounding::TowardZero) {
      return fixedValue / kScale;
    }

    if (rounding == SeqFixedPointRounding::Nearest) {
      const ValueType halfScale = kScale / 2;
      return fixedValue >= ValueType {}
          ? static_cast<ValueType>((fixedValue + halfScale) / kScale)
          : static_cast<ValueType>((fixedValue - halfScale) / kScale);
    }

    if (fixedValue >= ValueType {}) {
      return fixedValue / kScale;
    }

    return static_cast<ValueType>(-((static_cast<ValueType>(-fixedValue) + kScale - 1) /
                                   kScale));
  }

  SeqAutomatedValue<ValueType> m_value;
  SeqFixedPointPolicy m_policy;
};
