/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/base/Types.h"

#include <algorithm>
#include <utility>

namespace vgmtrans::core {

enum class SequenceMotionStatus {
  Inactive,
  Delayed,
  Running,
  Finished,
};

enum class SequenceMotionMode {
  TargetOverTicks,
  TargetOverTicksWithStep,
  TargetByStep,
};

template <typename ValueType>
struct SequenceMotionTick {
  SequenceMotionStatus status = SequenceMotionStatus::Inactive;
  ValueType previous {};
  ValueType current {};
  bool changed = false;

  [[nodiscard]] bool active() const {
    return status != SequenceMotionStatus::Inactive;
  }

  [[nodiscard]] bool shouldApply(bool applyDelayedStep = false) const {
    return active() && (applyDelayedStep || status != SequenceMotionStatus::Delayed);
  }
};

template <typename ValueType>
struct SequenceMotionPlan {
  ValueType target {};
  ValueType step {};
  u32 ticks = 0;
  u32 delay = 0;
  SequenceMotionMode mode = SequenceMotionMode::TargetOverTicks;

  [[nodiscard]] static SequenceMotionPlan targetOverTicks(ValueType targetValue, u32 tickCount,
                                                          u32 delayTicks = 0) {
    return {targetValue, {}, tickCount, delayTicks, SequenceMotionMode::TargetOverTicks};
  }

  [[nodiscard]] static SequenceMotionPlan targetOverTicksWithStep(ValueType targetValue, ValueType stepValue,
                                                                  u32 tickCount, u32 delayTicks = 0) {
    return {targetValue, stepValue, tickCount, delayTicks, SequenceMotionMode::TargetOverTicksWithStep};
  }

  [[nodiscard]] static SequenceMotionPlan targetByStep(ValueType targetValue, ValueType stepValue,
                                                       u32 delayTicks = 0) {
    return {targetValue, stepValue, 0, delayTicks, SequenceMotionMode::TargetByStep};
  }

  [[nodiscard]] bool usesTicks() const {
    return mode != SequenceMotionMode::TargetByStep;
  }
};

template <typename ValueType>
class SequenceLinearMotion {
public:
  void reset(ValueType current = {}) { setCurrent(current); }

  void setCurrent(ValueType current) {
    current_ = current;
    clear();
  }

  void setCurrentPreservingMotion(ValueType current) {
    current_ = current;
  }

  void clear() {
    target_ = current_;
    step_ = {};
    delay_ = 0;
    ticksRemaining_ = 0;
    mode_ = SequenceMotionMode::TargetOverTicks;
  }

  SequenceMotionTick<ValueType> begin(const SequenceMotionPlan<ValueType>& plan) {
    const ValueType previous = current_;
    target_ = plan.target;
    delay_ = plan.delay;
    ticksRemaining_ = plan.ticks;
    mode_ = plan.mode;

    if (plan.usesTicks() && plan.ticks == 0) {
      setCurrent(plan.target);
      return {SequenceMotionStatus::Finished, previous, current_, current_ != previous};
    }

    step_ = plan.mode == SequenceMotionMode::TargetOverTicks
                ? static_cast<ValueType>((plan.target - current_) / static_cast<ValueType>(plan.ticks))
                : plan.step;
    if (!plan.usesTicks() && (current_ == target_ || step_ == ValueType{})) {
      if (current_ == target_) {
        clear();
      } else {
        setCurrent(plan.target);
      }
      return {SequenceMotionStatus::Finished, previous, current_, current_ != previous};
    }

    return {plan.delay != 0 ? SequenceMotionStatus::Delayed : SequenceMotionStatus::Running, previous, current_,
            false};
  }

  [[nodiscard]] bool active() const {
    return delay_ != 0 ||
           (mode_ == SequenceMotionMode::TargetByStep ? step_ != ValueType{} : ticksRemaining_ != 0);
  }

  [[nodiscard]] ValueType current() const { return current_; }

  [[nodiscard]] SequenceMotionTick<ValueType> tick() {
    const ValueType previous = current_;

    if (delay_ != 0) {
      --delay_;
      return {SequenceMotionStatus::Delayed, previous, current_, false};
    }

    if (mode_ != SequenceMotionMode::TargetByStep) {
      if (ticksRemaining_ == 0) {
        return {SequenceMotionStatus::Inactive, previous, current_, false};
      }

      --ticksRemaining_;
      if (ticksRemaining_ == 0) {
        current_ = target_;
        return {SequenceMotionStatus::Finished, previous, current_, current_ != previous};
      }

      current_ = static_cast<ValueType>(current_ + step_);
      return {SequenceMotionStatus::Running, previous, current_, current_ != previous};
    }

    if (step_ == ValueType{}) {
      return {SequenceMotionStatus::Inactive, previous, current_, false};
    }

    current_ = static_cast<ValueType>(current_ + step_);
    if ((step_ > ValueType{} && current_ >= target_) || (step_ < ValueType{} && current_ <= target_)) {
      current_ = target_;
      step_ = {};
      return {SequenceMotionStatus::Finished, previous, current_, current_ != previous};
    }

    return {SequenceMotionStatus::Running, previous, current_, current_ != previous};
  }

  template <typename Apply>
  SequenceMotionTick<ValueType> tickChanged(Apply&& apply, bool applyDelayedStep = false) {
    const auto motionTick = tick();
    if (motionTick.shouldApply(applyDelayedStep) && motionTick.changed) {
      std::forward<Apply>(apply)(motionTick.current);
    }
    return motionTick;
  }

private:
  ValueType current_{};
  ValueType target_{};
  ValueType step_{};
  u32 delay_ = 0;
  u32 ticksRemaining_ = 0;
  SequenceMotionMode mode_ = SequenceMotionMode::TargetOverTicks;
};

template <typename ValueType = s32, unsigned FractionBits = 8>
struct SequenceFixedPointMotion {
  ValueType targetRaw {};
  ValueType stepFixed {};
  u32 ticks = 0;
  u32 delay = 0;
  SequenceMotionMode mode = SequenceMotionMode::TargetOverTicks;

  [[nodiscard]] static SequenceFixedPointMotion toRawTarget(ValueType targetValue, u32 tickCount, u32 delayTicks = 0) {
    return {targetValue, {}, tickCount, delayTicks, SequenceMotionMode::TargetOverTicks};
  }

  [[nodiscard]] static SequenceFixedPointMotion toRawTargetByFixedStep(ValueType targetValue, ValueType stepValue,
                                                                       u32 delayTicks = 0) {
    return {targetValue, stepValue, 0, delayTicks, SequenceMotionMode::TargetByStep};
  }
};

enum class SequenceFixedPointRounding {
  Floor,
  TowardZero,
  Nearest,
};

template <typename ValueType = s32, unsigned FractionBits = 8>
class SequenceFixedPointAutomation {
public:
  static constexpr ValueType kScale = static_cast<ValueType>(1) << FractionBits;

  static constexpr ValueType toFixed(ValueType rawValue) {
    return rawValue * kScale;
  }

  void reset(ValueType rawCurrent = {}) { value_.reset(toFixed(rawCurrent)); }
  void setCurrentRaw(ValueType rawCurrent) { value_.setCurrent(toFixed(rawCurrent)); }

  [[nodiscard]] bool active() const { return value_.active(); }
  [[nodiscard]] ValueType currentFixed() const { return value_.current(); }
  [[nodiscard]] ValueType currentRaw() const { return rawFromFixed(value_.current()); }

  void setRounding(SequenceFixedPointRounding rounding) {
    rounding_ = rounding;
  }

  SequenceMotionTick<ValueType> begin(const SequenceFixedPointMotion<ValueType, FractionBits>& rawMotion) {
    // Drivers retarget from the rounded raw value, discarding the old fraction.
    // Linear motion then computes the step in fixed-point units.
    value_.setCurrentPreservingMotion(toFixed(currentRaw()));
    return value_.begin(SequenceMotionPlan<ValueType>{
        toFixed(rawMotion.targetRaw),
        rawMotion.stepFixed,
        rawMotion.ticks,
        rawMotion.delay,
        rawMotion.mode,
    });
  }

  [[nodiscard]] SequenceMotionTick<ValueType> tick() { return value_.tick(); }

  template <typename ApplyRaw>
  SequenceMotionTick<ValueType> tickRaw(ApplyRaw&& applyRaw, bool applyDelayedStep = false) {
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
  [[nodiscard]] ValueType rawFromFixed(ValueType fixedValue) const {
    if (rounding_ == SequenceFixedPointRounding::TowardZero) {
      return fixedValue / kScale;
    }
    if (rounding_ == SequenceFixedPointRounding::Nearest) {
      const ValueType halfScale = kScale / 2;
      return fixedValue >= ValueType{} ? static_cast<ValueType>((fixedValue + halfScale) / kScale)
                                       : static_cast<ValueType>((fixedValue - halfScale) / kScale);
    }
    if (fixedValue >= ValueType{}) {
      return fixedValue / kScale;
    }
    return static_cast<ValueType>(-((static_cast<ValueType>(-fixedValue) + kScale - 1) / kScale));
  }

  SequenceLinearMotion<ValueType> value_;
  SequenceFixedPointRounding rounding_ = SequenceFixedPointRounding::Floor;
};

}  // namespace vgmtrans::core
