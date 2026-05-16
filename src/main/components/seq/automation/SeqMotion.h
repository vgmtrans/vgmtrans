/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>

// Low-level, MIDI-agnostic primitives for one value changing over driver ticks.
// Higher-level automation classes add source-format policy and MIDI output.
enum class SeqMotionStatus {
  Inactive,
  Delayed,
  Running,
  Finished,
};

enum class SeqMotionMode {
  TargetOverTicks,
  TargetOverTicksWithStep,
  TargetByStep,
};

template <typename ValueType>
struct SeqMotionTick {
  SeqMotionStatus status = SeqMotionStatus::Inactive;
  ValueType previous {};
  ValueType current {};
  bool changed = false;

  [[nodiscard]] bool active() const {
    return status != SeqMotionStatus::Inactive;
  }

  // Return whether callers should apply output for this tick. Delayed ticks
  // are skipped unless the caller explicitly asks to apply them.
  [[nodiscard]] bool shouldApply(bool applyDelayedStep = false) const {
    return active() && (applyDelayedStep || status != SeqMotionStatus::Delayed);
  }
};

template <typename ValueType>
struct SeqMotionPlan {
  ValueType target {};
  ValueType step {};
  uint32_t ticks = 0;
  uint32_t delay = 0;
  SeqMotionMode mode = SeqMotionMode::TargetOverTicks;
  bool snapToTarget = true;

  // Compute the per-tick step from the current value when begin() is called.
  static SeqMotionPlan targetOverTicks(ValueType targetValue,
                                       uint32_t tickCount,
                                       uint32_t delayTicks = 0) {
    return {targetValue, {}, tickCount, delayTicks, SeqMotionMode::TargetOverTicks, true};
  }

  // Use the supplied step for tickCount ticks, then snap to target.
  static SeqMotionPlan targetOverTicksWithStep(ValueType targetValue,
                                               ValueType stepValue,
                                               uint32_t tickCount,
                                               uint32_t delayTicks = 0) {
    return {targetValue, stepValue, tickCount, delayTicks, SeqMotionMode::TargetOverTicksWithStep, true};
  }

  // Use the supplied step for tickCount ticks and leave the final stepped value as-is.
  static SeqMotionPlan targetOverTicksWithStepNoSnap(ValueType targetValue,
                                                     ValueType stepValue,
                                                     uint32_t tickCount,
                                                     uint32_t delayTicks = 0) {
    return {targetValue, stepValue, tickCount, delayTicks, SeqMotionMode::TargetOverTicksWithStep, false};
  }

  // Use the supplied step as-is until the value reaches or crosses target.
  static SeqMotionPlan targetByStep(ValueType targetValue,
                                    ValueType stepValue,
                                    uint32_t delayTicks = 0) {
    return {targetValue, stepValue, 0, delayTicks, SeqMotionMode::TargetByStep, true};
  }

  [[nodiscard]] bool usesTicks() const {
    return mode != SeqMotionMode::TargetByStep;
  }

  [[nodiscard]] bool usesStepUntilTarget() const {
    return mode == SeqMotionMode::TargetByStep;
  }
};

template <typename ValueType>
class SeqLinearMotion {
 public:
  void reset(ValueType current = {}) {
    m_current = current;
    clear();
  }

  // Set current value and clear any active motion.
  void setCurrent(ValueType current) {
    m_current = current;
    clear();
  }

  // Replace the current value without clearing target, step, delay, or remaining ticks.
  void setCurrentPreservingMotion(ValueType current) {
    m_current = current;
  }

  // Clear motion state and make the current value the target.
  void clear() {
    m_target = m_current;
    m_step = {};
    m_delay = 0;
    m_ticksRemaining = 0;
    m_mode = SeqMotionMode::TargetOverTicks;
  }

  // Start the plan. Zero-tick plans set current immediately and return Finished.
  SeqMotionTick<ValueType> begin(const SeqMotionPlan<ValueType>& plan) {
    const ValueType previous = m_current;
    m_target = plan.target;
    m_delay = plan.delay;
    m_ticksRemaining = plan.ticks;
    m_mode = plan.mode;
    m_snapToTarget = plan.snapToTarget;

    if (plan.mode == SeqMotionMode::TargetOverTicks) {
      if (plan.ticks == 0) {
        setCurrent(plan.target);
        return {SeqMotionStatus::Finished, previous, m_current, m_current != previous};
      }
      m_step = static_cast<ValueType>((plan.target - m_current) /
                                      static_cast<ValueType>(plan.ticks));
      return {plan.delay != 0 ? SeqMotionStatus::Delayed : SeqMotionStatus::Running,
              previous,
              m_current,
              false};
    }

    if (plan.mode == SeqMotionMode::TargetOverTicksWithStep) {
      if (plan.ticks == 0) {
        setCurrent(plan.target);
        return {SeqMotionStatus::Finished, previous, m_current, m_current != previous};
      }
      m_step = plan.step;
      return {plan.delay != 0 ? SeqMotionStatus::Delayed : SeqMotionStatus::Running,
              previous,
              m_current,
              false};
    }

    m_step = plan.step;
    if (m_current == m_target) {
      clear();
      return {SeqMotionStatus::Finished, previous, m_current, false};
    }

    if (m_step == ValueType {}) {
      setCurrent(plan.target);
      return {SeqMotionStatus::Finished, previous, m_current, m_current != previous};
    }

    return {plan.delay != 0 ? SeqMotionStatus::Delayed : SeqMotionStatus::Running,
            previous,
            m_current,
            false};
  }

  [[nodiscard]] bool active() const {
    return m_delay != 0 ||
           (m_mode == SeqMotionMode::TargetByStep ? m_step != ValueType {}
                                                  : m_ticksRemaining != 0);
  }

  [[nodiscard]] ValueType current() const { return m_current; }
  [[nodiscard]] ValueType target() const { return m_target; }
  [[nodiscard]] ValueType step() const { return m_step; }
  [[nodiscard]] uint32_t delayRemaining() const { return m_delay; }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_ticksRemaining; }
  [[nodiscard]] SeqMotionMode mode() const { return m_mode; }
  [[nodiscard]] bool usesTicks() const { return m_mode != SeqMotionMode::TargetByStep; }

  // Advance one driver tick. Delay ticks decrement delay; active motions update
  // current and snap to target when they finish.
  SeqMotionTick<ValueType> tick() {
    const ValueType previous = m_current;

    if (m_delay != 0) {
      m_delay -= 1;
      return {SeqMotionStatus::Delayed, previous, m_current, false};
    }

    if (m_mode != SeqMotionMode::TargetByStep) {
      if (m_ticksRemaining == 0) {
        return {SeqMotionStatus::Inactive, previous, m_current, false};
      }

      m_ticksRemaining -= 1;
      if (m_ticksRemaining == 0) {
        if (m_snapToTarget) {
          m_current = m_target;
        }
        else {
          m_current = static_cast<ValueType>(m_current + m_step);
        }
        return {SeqMotionStatus::Finished, previous, m_current, m_current != previous};
      }

      m_current = static_cast<ValueType>(m_current + m_step);
      return {SeqMotionStatus::Running, previous, m_current, m_current != previous};
    }

    if (m_step == ValueType {}) {
      return {SeqMotionStatus::Inactive, previous, m_current, false};
    }

    m_current = static_cast<ValueType>(m_current + m_step);
    if ((m_step > ValueType {} && m_current >= m_target) ||
        (m_step < ValueType {} && m_current <= m_target)) {
      m_current = m_target;
      m_step = {};
      return {SeqMotionStatus::Finished, previous, m_current, m_current != previous};
    }

    return {SeqMotionStatus::Running, previous, m_current, m_current != previous};
  }
 private:
  ValueType m_current {};
  ValueType m_target {};
  ValueType m_step {};
  uint32_t m_delay = 0;
  uint32_t m_ticksRemaining = 0;
  SeqMotionMode m_mode = SeqMotionMode::TargetOverTicks;
  bool m_snapToTarget = true;
};
