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
#include <utility>

namespace vgmtrans::core {

// Source-domain state shared by LFO depth implementations. Depth values are
// deliberately opaque signed integers: each format chooses its own fixed-point
// scale and converts the live value to semitones or decibels itself.
//
// Some drivers install a fade duration once and restart it on every note. Keeping
// that duration separate from the active motion preserves that behavior while
// allowing each format to choose the initial depth and delay. The
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
  void resetCurrentDepth() { fade_.reset(targetDepth_); }

  // Cancels both future restarts and live motion while preserving the current
  // depth value.
  void clearFade() {
    fadeTicks_ = 0;
    fade_.clear();
    fade_.clearAutomation();
  }

  // Installs the duration used by subsequent restart calls. Zero removes the
  // reusable fade without interrupting a fade already in progress.
  void configureLinearFade(u32 ticks) { fadeTicks_ = ticks; }

  [[nodiscard]] bool fadeConfigured() const { return fadeTicks_ != 0; }
  [[nodiscard]] u32 fadeDurationTicks() const { return fadeTicks_; }

  // Restarts the configured fade. False means no duration is configured and
  // the live depth is left unchanged.
  [[nodiscard]] bool restartFade(u32 delayTicks, s32 initialDepth = 0) {
    if (fadeTicks_ == 0) {
      return false;
    }
    fade_.reset(initialDepth);
    fade_.begin(SequenceMotionPlan<s32>::targetOverTicksWithStep(
        targetDepth_, targetDepth_ / fadeTicks_, fadeTicks_, delayTicks));
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
  u32 fadeTicks_ = 0;
  PerformanceBoundValue<SequenceLinearMotion<s32>> fade_;
  double lastPhysicalDepth_ = 0.0;
};

}  // namespace vgmtrans::core
