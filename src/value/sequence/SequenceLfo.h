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

// Driver-facing LFO state shared by sequence formats whose commands use byte
// delay/rate/depth parameters. Physical rate, depth, phase, and waveform remain
// format policy; this type only owns the common parameter and depth-fade
// lifecycle.
//
// Some drivers install a fade once and restart it on every note. Keeping its
// motion plan separate from the active motion preserves that behavior while
// allowing each format to choose its fixed-point scale, initial depth, and
// delay.
struct SequenceLfoState {
  void configure(u8 delayValue, u8 rateValue, u8 depthValue) {
    delay = delayValue;
    rate = rateValue;
    depth = depthValue;
    clearFade();
  }

  [[nodiscard]] bool active() const { return rate != 0 && depth != 0; }
  [[nodiscard]] s32 scaledDepth(u8 fractionalBits = 0) const { return static_cast<s32>(depth) << fractionalBits; }

  [[nodiscard]] s32 currentDepth(u8 fractionalBits = 0) const {
    return std::clamp(fade.current(), s32{0}, scaledDepth(fractionalBits));
  }

  void setCurrentDepth(u8 fractionalBits = 0) { fade.setCurrent(scaledDepth(fractionalBits)); }

  void clearFade() {
    reusableFade.reset();
    fade.clearMotion();
    fade.clearAutomation();
  }

  void setFade(u32 ticks, s32 target, s32 step) {
    reusableFade = ticks == 0 ? std::nullopt
                              : std::optional{SequenceMotionPlan<s32>::targetOverTicksWithStep(target, step, ticks)};
  }

  void setFadeToDepth(u32 ticks, u8 fractionalBits = 0) {
    const s32 target = scaledDepth(fractionalBits);
    setFade(ticks, target, ticks == 0 ? 0 : target / static_cast<s32>(ticks));
  }

  void beginFade(u32 delayTicks, s32 initialDepth = 0) {
    if (!reusableFade) {
      fade.clearMotion();
      return;
    }
    fade.setCurrent(initialDepth);
    auto plan = *reusableFade;
    plan.delay = delayTicks;
    static_cast<void>(fade.begin(plan));
  }

  template <typename EmitDepth>
  void emitDepth(double value, EmitDepth&& emit, bool force = false, double tolerance = 0.000001) {
    if (!force && lastPhysicalDepth && std::abs(*lastPhysicalDepth - value) < tolerance) {
      return;
    }
    std::forward<EmitDepth>(emit)(value);
    lastPhysicalDepth = value;
  }

  u8 delay = 0;
  u8 rate = 0;
  u8 depth = 0;
  std::optional<SequenceMotionPlan<s32>> reusableFade;
  PerformanceBoundMotion<SequenceAutomatedValue<s32>> fade;
  std::optional<double> lastPhysicalDepth;
};

}  // namespace vgmtrans::core
