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

namespace vgmtrans::seq {

template <typename ValueType>
class SeqAutomatedValue {
 public:
  void reset(ValueType current = {}) { m_motion.reset(current); }
  void jumpTo(ValueType current) { m_motion.jumpTo(current); }
  void setAccumulator(ValueType current) { m_motion.setAccumulator(current); }
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
    if (tick.status == SeqMotionStatus::Finished && tick.changed) {
      std::forward<Apply>(apply)(tick.current);
    }
    return tick;
  }

  SeqMotionTick<ValueType> tick() { return m_motion.tick(); }

  template <typename Apply>
  SeqMotionTick<ValueType> tick(Apply&& apply, bool applyDelayedStep = false) {
    const auto motionTick = tick();
    if (motionTick.shouldApply(applyDelayedStep)) {
      std::forward<Apply>(apply)(motionTick.current);
    }
    return motionTick;
  }

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

template <typename ValueType>
class SeqCachedEmitter {
 public:
  void reset(ValueType current = {}) { m_current = current; }
  [[nodiscard]] ValueType current() const { return m_current; }
  void setCurrent(ValueType current) { m_current = current; }

  template <typename Emit>
  bool emit(ValueType value, Emit&& emitValue, bool force = false) {
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

  void setToTarget(uint32_t ticks, ValueType targetFromZero) {
    set(ticks, ticks == 0 ? ValueType {} : static_cast<ValueType>(targetFromZero /
                                                                  static_cast<ValueType>(ticks)));
  }

  [[nodiscard]] bool active() const { return m_ticks != 0; }
  [[nodiscard]] uint32_t ticks() const { return m_ticks; }
  [[nodiscard]] ValueType step() const { return m_step; }

  [[nodiscard]] SeqMotionPlan<ValueType> instantiate(ValueType target, uint32_t delay = 0) const {
    return SeqMotionPlan<ValueType>::targetOverTicksWithStep(target, m_step, m_ticks, delay);
  }

 private:
  uint32_t m_ticks = 0;
  ValueType m_step {};
};

template <typename ValueType, unsigned FractionBits = 8>
struct SeqFixedPointMotion {
  static SeqMotionPlan<ValueType> toRawTarget(ValueType rawTarget,
                                             uint32_t ticks,
                                             uint32_t delay = 0) {
    return SeqMotionPlan<ValueType>::targetOverTicks(rawTarget, ticks, delay);
  }

  static SeqMotionPlan<ValueType> toRawTargetByFixedStep(ValueType rawTarget,
                                                        ValueType fixedStep,
                                                        uint32_t delay = 0) {
    return SeqMotionPlan<ValueType>::targetByStep(rawTarget, fixedStep, delay);
  }
};

template <typename ValueType = int32_t, unsigned FractionBits = 8>
class SeqFixedPointAutomation {
 public:
  static constexpr ValueType kScale = static_cast<ValueType>(1) << FractionBits;

  static constexpr ValueType toFixed(ValueType rawValue) {
    return rawValue << FractionBits;
  }

  static constexpr ValueType toRaw(ValueType fixedValue) {
    return fixedValue >> FractionBits;
  }

  void reset(ValueType rawCurrent = {}) { m_value.reset(toFixed(rawCurrent)); }
  void jumpToRaw(ValueType rawCurrent) { m_value.jumpTo(toFixed(rawCurrent)); }
  void setAccumulatorFixed(ValueType fixedCurrent) { m_value.setAccumulator(fixedCurrent); }
  void clearMotion() { m_value.clearMotion(); }

  [[nodiscard]] bool active() const { return m_value.active(); }
  [[nodiscard]] ValueType currentFixed() const { return m_value.current(); }
  [[nodiscard]] ValueType currentRaw() const { return toRaw(m_value.current()); }
  [[nodiscard]] ValueType targetFixed() const { return m_value.target(); }
  [[nodiscard]] ValueType targetRaw() const { return toRaw(m_value.target()); }
  [[nodiscard]] ValueType step() const { return m_value.step(); }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_value.ticksRemaining(); }
  [[nodiscard]] bool usesTicks() const { return m_value.usesTicks(); }

  [[nodiscard]] ValueType currentRawFixed() const {
    return toFixed(currentRaw());
  }

  [[nodiscard]] ValueType stepToRawTarget(ValueType rawTarget, uint32_t ticks) const {
    if (ticks == 0) {
      return {};
    }
    return static_cast<ValueType>((toFixed(rawTarget) - currentRawFixed()) /
                                  static_cast<ValueType>(ticks));
  }

  SeqMotionTick<ValueType> begin(const SeqMotionPlan<ValueType>& rawMotion) {
    m_value.setAccumulator(currentRawFixed());

    SeqMotionPlan<ValueType> fixedMotion = rawMotion;
    fixedMotion.target = toFixed(rawMotion.target);
    if (rawMotion.mode == SeqMotionMode::TargetOverTicks) {
      fixedMotion.mode = SeqMotionMode::TargetOverTicksWithStep;
      fixedMotion.step = stepToRawTarget(rawMotion.target, rawMotion.ticks);
    }

    return m_value.begin(fixedMotion);
  }

  template <typename ApplyRaw>
  SeqMotionTick<ValueType> begin(const SeqMotionPlan<ValueType>& rawMotion, ApplyRaw&& applyRaw) {
    const auto motionTick = begin(rawMotion);
    if (motionTick.status == SeqMotionStatus::Finished && motionTick.changed) {
      std::forward<ApplyRaw>(applyRaw)(toRaw(motionTick.current));
    }
    return motionTick;
  }

  SeqMotionTick<ValueType> tick() { return m_value.tick(); }

  template <typename ApplyRaw>
  SeqMotionTick<ValueType> tickRaw(ApplyRaw&& applyRaw, bool applyDelayedStep = false) {
    const auto motionTick = tick();
    if (motionTick.shouldApply(applyDelayedStep)) {
      std::forward<ApplyRaw>(applyRaw)(toRaw(motionTick.current));
    }
    return motionTick;
  }

  template <typename ApplyRaw>
  SeqMotionTick<ValueType> tickRawChanged(ApplyRaw&& applyRaw, bool applyDelayedStep = false) {
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
  SeqAutomatedValue<ValueType> m_value;
};

}  // namespace vgmtrans::seq
