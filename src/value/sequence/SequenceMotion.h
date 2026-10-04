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
  explicit SequenceLinearMotion(ValueType current) : current_(current) {}

  void reset(ValueType current = {}) {
    current_ = current;
    clear();
  }

  void clear() {
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
                ? static_cast<ValueType>((plan.target - current_) / static_cast<s64>(plan.ticks))
                : plan.step;
    if (!plan.usesTicks() && (current_ == target_ || step_ == ValueType{})) {
      if (current_ != target_) {
        current_ = target_;
      }
      clear();
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

// Source motion with a signed 32-bit accumulator and eight fractional bits.
class SequenceFixedPointAutomation {
public:
  SequenceFixedPointAutomation() = default;
  explicit SequenceFixedPointAutomation(s32 rawCurrent) : value_(toFixed(rawCurrent)) {}

  static constexpr s32 kScale = 256;

  static constexpr s32 toFixed(s32 rawValue) {
    return rawValue * kScale;
  }

  // Plans use the accumulator's fixed-point units. Convert the raw target
  // here; a driver-supplied fixed step already has the correct scale.
  [[nodiscard]] static SequenceMotionPlan<s32> toRawTarget(s32 target, u32 ticks, u32 delay = 0) {
    return SequenceMotionPlan<s32>::targetOverTicks(toFixed(target), ticks, delay);
  }

  [[nodiscard]] static SequenceMotionPlan<s32> toRawTargetByFixedStep(s32 target, s32 step, u32 delay = 0) {
    return SequenceMotionPlan<s32>::targetByStep(toFixed(target), step, delay);
  }

  void reset(s32 rawCurrent = {}) { value_.reset(toFixed(rawCurrent)); }

  [[nodiscard]] bool active() const { return value_.active(); }
  [[nodiscard]] s32 currentFixed() const { return value_.current(); }
  [[nodiscard]] s32 currentRaw() const { return rawFromFixed(value_.current()); }

  void setRounding(SequenceFixedPointRounding rounding) {
    rounding_ = rounding;
  }

  SequenceMotionTick<s32> begin(const SequenceMotionPlan<s32>& plan) {
    // Drivers retarget from the rounded raw value, discarding the old fraction.
    value_.reset(toFixed(currentRaw()));
    return value_.begin(plan);
  }

  [[nodiscard]] SequenceMotionTick<s32> tick() { return value_.tick(); }

  template <typename ApplyRaw>
  SequenceMotionTick<s32> tickRaw(ApplyRaw&& applyRaw) {
    const s32 previousRaw = currentRaw();
    const auto motionTick = tick();
    const s32 nextRaw = currentRaw();
    if (nextRaw != previousRaw) {
      std::forward<ApplyRaw>(applyRaw)(nextRaw);
    }
    return motionTick;
  }

private:
  [[nodiscard]] s32 rawFromFixed(s32 fixedValue) const {
    const s32 whole = fixedValue / kScale;
    const s32 fraction = fixedValue % kScale;
    if (rounding_ == SequenceFixedPointRounding::TowardZero) {
      return whole;
    }
    if (rounding_ == SequenceFixedPointRounding::Nearest) {
      const s32 halfScale = kScale / 2;
      if (fraction >= halfScale) {
        return whole + 1;
      }
      return whole - (fraction <= -halfScale);
    }
    return whole - (fraction < 0);
  }

  SequenceLinearMotion<s32> value_;
  SequenceFixedPointRounding rounding_ = SequenceFixedPointRounding::Floor;
};

}  // namespace vgmtrans::core
