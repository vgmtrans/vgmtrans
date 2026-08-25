/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/sequence/SequenceMotion.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace vgmtrans::core {

// Source-domain state shared by LFO depth implementations. Depth values are
// deliberately opaque signed integers: each format chooses its own fixed-point
// scale and converts the live value to semitones or decibels itself.
//
// Some drivers install a fade once and restart it on every note. Keeping its
// motion plan separate from the active motion preserves that behavior while
// allowing each format to choose the step, initial depth, and delay. The
// automation binding follows the live motion because each note may attach its
// restarted fade to a different performance note.
//
// This class intentionally does not store an LFO's source bytes, determine
// whether it is enabled, or interpret its rate, waveform, or physical depth.
// Those rules belong to the format. It only owns the reusable lifecycle common
// to depth in different drivers.
class SequenceLfoDepthFadeState {
public:
  // Establishes a new source-domain target and cancels the previous fade.
  // A depth is a magnitude, so negative targets collapse to zero. The physical
  // output cache is preserved so equivalent repeated commands stay compact;
  // formats can force emission when the command itself must remain observable.
  void resetDepth(s32 targetDepth) {
    targetDepth_ = std::max(targetDepth, s32{0});
    clearFade();
    resetCurrentDepth();
  }

  [[nodiscard]] s32 targetDepth() const { return targetDepth_; }
  [[nodiscard]] s32 currentDepth() const { return std::clamp(fade_.current(), s32{0}, targetDepth_); }
  void resetCurrentDepth() { fade_.setCurrent(targetDepth_); }

  // Cancels both future restarts and live motion while preserving the current
  // depth value.
  void clearFade() {
    fadePlan_.reset();
    fade_.clear();
    fade_.clearAutomation();
  }

  // Installs the plan used by subsequent restart calls. A zero duration
  // removes the reusable plan without interrupting a fade already in progress.
  void configureFade(u32 ticks, s32 step) {
    if (ticks == 0) {
      fadePlan_.reset();
      return;
    }
    fadePlan_ = SequenceMotionPlan<s32>::targetOverTicksWithStep(targetDepth_, step, ticks);
  }

  void configureLinearFade(u32 ticks) { configureFade(ticks, ticks == 0 ? 0 : targetDepth_ / static_cast<s32>(ticks)); }

  [[nodiscard]] bool fadeConfigured() const { return fadePlan_.has_value(); }
  [[nodiscard]] u32 fadeDurationTicks() const { return fadePlan_ ? fadePlan_->ticks : 0; }

  // Restarts the installed plan without consuming it. False means no reusable
  // plan is configured and the live depth is left unchanged.
  [[nodiscard]] bool restartFade(u32 delayTicks, s32 initialDepth = 0) {
    if (!fadePlan_) {
      return false;
    }
    fade_.setCurrent(initialDepth);
    auto plan = *fadePlan_;
    plan.delay = delayTicks;
    static_cast<void>(fade_.begin(plan));
    return true;
  }

  [[nodiscard]] SequenceMotionTick<s32> tickFade() { return fade_.tick(); }

  void bindFade(PerformanceAutomationBinding binding) { fade_.bind(std::move(binding)); }
  void interruptFadeAutomationAt(u64 tick) { fade_.interruptAutomationAt(tick); }
  [[nodiscard]] PerformanceEmitter fadeOutput(const PerformanceEmitter& out) const { return fade_.output(out); }

  // Source arithmetic may produce many successive values that map to the same
  // physical depth. Suppressing those duplicates keeps the performance model
  // compact without making the format surrender its conversion formula.
  template <typename EmitDepth>
  void emitPhysicalDepth(double value, EmitDepth&& emit, bool force = false, double tolerance = 0.000001) {
    if (!force && std::abs(lastPhysicalDepth_ - value) < tolerance) {
      return;
    }
    std::forward<EmitDepth>(emit)(value);
    lastPhysicalDepth_ = value;
  }

private:
  s32 targetDepth_ = 0;
  std::optional<SequenceMotionPlan<s32>> fadePlan_;
  PerformanceBoundValue<SequenceLinearMotion<s32>> fade_;
  double lastPhysicalDepth_ = 0.0;
};

}  // namespace vgmtrans::core
