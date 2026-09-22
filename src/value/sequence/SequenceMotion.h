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
  ValueType current {};
  bool changed = false;

  [[nodiscard]] bool shouldApply() const {
    return status == SequenceMotionStatus::Running || status == SequenceMotionStatus::Finished;
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
  SequenceLinearMotion() = default;
  explicit SequenceLinearMotion(ValueType current) : current_(current), target_(current) {}

  void reset(ValueType current = {}) {
    current_ = current;
    clear();
  }

  void clear() {
    target_ = current_;
    step_ = {};
    delay_ = 0;
    ticksRemaining_ = 0;
  }

  SequenceMotionTick<ValueType> begin(const SequenceMotionPlan<ValueType>& plan) {
    const ValueType previous = current_;
    target_ = plan.target;
    delay_ = plan.delay;
    ticksRemaining_ = plan.usesTicks() ? plan.ticks : 0;

    if (plan.usesTicks() && plan.ticks == 0) {
      reset(plan.target);
      return {SequenceMotionStatus::Finished, current_, current_ != previous};
    }

    step_ = plan.mode == SequenceMotionMode::TargetOverTicks
                ? static_cast<ValueType>((plan.target - current_) / static_cast<ValueType>(plan.ticks))
                : plan.step;
    if (!plan.usesTicks() && (current_ == target_ || step_ == ValueType{})) {
      if (current_ == target_) {
        clear();
      } else {
        reset(plan.target);
      }
      return {SequenceMotionStatus::Finished, current_, current_ != previous};
    }

    return {plan.delay != 0 ? SequenceMotionStatus::Delayed : SequenceMotionStatus::Running, current_, false};
  }

  [[nodiscard]] bool active() const { return delay_ != 0 || ticksRemaining_ != 0 || step_ != ValueType{}; }

  [[nodiscard]] ValueType current() const { return current_; }

  [[nodiscard]] SequenceMotionTick<ValueType> tick() {
    const ValueType previous = current_;

    if (delay_ != 0) {
      --delay_;
      return {SequenceMotionStatus::Delayed, current_, false};
    }

    if (ticksRemaining_ != 0) {
      --ticksRemaining_;
      if (ticksRemaining_ == 0) {
        current_ = target_;
        step_ = {};
        return {SequenceMotionStatus::Finished, current_, current_ != previous};
      }

      current_ = static_cast<ValueType>(current_ + step_);
      return {SequenceMotionStatus::Running, current_, current_ != previous};
    }

    if (step_ == ValueType{}) {
      return {SequenceMotionStatus::Inactive, current_, false};
    }

    current_ = static_cast<ValueType>(current_ + step_);
    if ((step_ > ValueType{} && current_ >= target_) || (step_ < ValueType{} && current_ <= target_)) {
      current_ = target_;
      step_ = {};
      return {SequenceMotionStatus::Finished, current_, current_ != previous};
    }

    return {SequenceMotionStatus::Running, current_, current_ != previous};
  }

  template <typename Apply>
  SequenceMotionTick<ValueType> tickChanged(Apply&& apply) {
    const auto motionTick = tick();
    if (motionTick.changed) {
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
};

enum class SequenceFixedPointRounding {
  Floor,
  TowardZero,
  Nearest,
};

template <typename ValueType = s32, unsigned FractionBits = 8>
class SequenceFixedPointAutomation {
public:
  SequenceFixedPointAutomation() = default;
  explicit SequenceFixedPointAutomation(ValueType rawCurrent) : value_(toFixed(rawCurrent)) {}

  static constexpr ValueType kScale = static_cast<ValueType>(1) << FractionBits;

  static constexpr ValueType toFixed(ValueType rawValue) {
    return rawValue * kScale;
  }

  // Plans use the accumulator's fixed-point units. Convert the raw target
  // here; a driver-supplied fixed step already has the correct scale.
  [[nodiscard]] static SequenceMotionPlan<ValueType> toRawTarget(ValueType target, u32 ticks, u32 delay = 0) {
    return SequenceMotionPlan<ValueType>::targetOverTicks(toFixed(target), ticks, delay);
  }

  [[nodiscard]] static SequenceMotionPlan<ValueType> toRawTargetByFixedStep(ValueType target, ValueType step,
                                                                            u32 delay = 0) {
    return SequenceMotionPlan<ValueType>::targetByStep(toFixed(target), step, delay);
  }

  void reset(ValueType rawCurrent = {}) { value_.reset(toFixed(rawCurrent)); }

  [[nodiscard]] bool active() const { return value_.active(); }
  [[nodiscard]] ValueType currentFixed() const { return value_.current(); }
  [[nodiscard]] ValueType currentRaw() const { return rawFromFixed(value_.current()); }

  void setRounding(SequenceFixedPointRounding rounding) {
    rounding_ = rounding;
  }

  SequenceMotionTick<ValueType> begin(const SequenceMotionPlan<ValueType>& plan) {
    // Drivers retarget from the rounded raw value, discarding the old fraction.
    value_.reset(toFixed(currentRaw()));
    return value_.begin(plan);
  }

  [[nodiscard]] SequenceMotionTick<ValueType> tick() { return value_.tick(); }

  template <typename ApplyRaw>
  SequenceMotionTick<ValueType> tickRaw(ApplyRaw&& applyRaw) {
    const ValueType previousRaw = currentRaw();
    const auto motionTick = tick();
    const ValueType nextRaw = currentRaw();
    if (nextRaw != previousRaw) {
      std::forward<ApplyRaw>(applyRaw)(nextRaw);
    }
    return motionTick;
  }

private:
  [[nodiscard]] ValueType rawFromFixed(ValueType fixedValue) const {
    const ValueType whole = fixedValue / kScale;
    const ValueType fraction = fixedValue % kScale;
    if (rounding_ == SequenceFixedPointRounding::TowardZero) {
      return whole;
    }
    if (rounding_ == SequenceFixedPointRounding::Nearest) {
      const ValueType halfScale = kScale / 2;
      if (fraction > ValueType{} && fraction >= halfScale) {
        return static_cast<ValueType>(whole + 1);
      }
      return static_cast<ValueType>(whole - (fraction < ValueType{} && fraction <= -halfScale));
    }
    return static_cast<ValueType>(whole - (fraction < ValueType{}));
  }

  SequenceLinearMotion<ValueType> value_;
  SequenceFixedPointRounding rounding_ = SequenceFixedPointRounding::Floor;
};

}  // namespace vgmtrans::core
