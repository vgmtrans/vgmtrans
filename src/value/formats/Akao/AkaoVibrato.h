/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/Akao/Akao.h"
#include "value/sequence/SequenceMotion.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vgmtrans::formats::akao {

// Translate driver settings into the shared pitch LFO. Standard waveforms
// approximate the native tables; SPU pitch rounding is intentionally omitted.
class AkaoVibrato {
public:
  // B4 selects timing and waveform, enables vibrato, and retains B5's depth.
  void start(core::PerformanceEmitter out, AkaoProfile profile, u8 delay, u16 period, u8 type) {
    using namespace core;
    enabled_ = true;
    period_.reset(period);
    auto context = updateContext();
    context.delay = LfoDelay{.ticks = delay, .tempoRelative = true};
    context.polarity = LfoPolarity::Bipolar;
    context.initialPhaseCycles = 0.0;
    // V3.2 B4 retains phase; earlier drivers restart the waveform.
    context.restartMode = profile.version32() ? LfoRestartMode::Delay : LfoRestartMode::PhaseAndDelay;
    auto& shape = context.shape.emplace();
    double cycleSteps;
    if (profile.version32()) {
      // V3.2 measures a quarter sine/triangle cycle or half a square cycle.
      // Types 4..7 add a four-step depth buildup to types 0..3.
      type &= 7;
      constexpr LfoWaveform waves[]{LfoWaveform::Square, LfoWaveform::Sine,
                                    LfoWaveform::Triangle, LfoWaveform::Noise};
      shape.waveform = waves[type & 3];
      cycleSteps = (type & 3) == 0 ? 2 : 4;
      context.steppedDepthAttackSteps = type >= 4 ? 4 : 0;
      if ((type & 3) == 3) {
        // The random table has half the other waveforms' depth. Generate a
        // repeatable substitute; type 7 increases depth every two table passes.
        u32 noise = 1;
        for (u32 i = 0; i < 256; ++i) {
          noise = noise * 1664525u + 1013904223u;
          shape.samples.push_back(static_cast<double>(noise >> 24) / 256.0 - 0.5);
        }
        cycleSteps = type == 7 ? 2 : 1;
        if (type == 7) {
          shape.samples.resize(512);
          std::copy_n(shape.samples.begin(), 256, shape.samples.begin() + 256);
        }
      }
    } else {
      // Before v3.2 the rate is the number of driver ticks per table entry.
      // V3 replaces the old aliases at 13..15 with noise and a shorter sine.
      type &= 15;
      if (!profile.version3OrLater() && type >= 13) type = type == 14 ? 8 : 9;
      constexpr u8 lengths[]{2, 2, 2, 2, 2, 2, 39, 39, 16, 16, 20, 20, 1, 1, 10, 10};
      cycleSteps = lengths[type];
      shape.waveform = LfoWaveform::Sine;
      if (type < 6) shape.waveform = LfoWaveform::Square;
      else if (type == 8 || type == 9) shape.waveform = LfoWaveform::Triangle;
      else if (type == 12 || type == 13) shape.waveform = LfoWaveform::Noise;
      context.steppedDepthAttackSteps = shape.waveform != LfoWaveform::Noise && type % 2 == 0 ? 4 : 0;
      if (type == 2 || type == 3) context.polarity = LfoPolarity::Positive;
      if (type == 4 || type == 5) {
        context.polarity = LfoPolarity::Negative;
        context.initialPhaseCycles = 0.5;
      }
    }
    cycleFrequencyHz_ = profile.driverTickHz() / cycleSteps;
    context.frequencyHz = frequency();
    emitDepth(out, std::move(context));
    // Synth export needs explicit timing events as well as the LFO context.
    out.vibratoRate(frequency(), updateContext());
    out.vibratoDelayTicks(delay);
  }

  // B5 changes depth without enabling vibrato or restarting phase and delay.
  void setDepth(core::PerformanceEmitter out, u8 value) {
    depth_ = value;
    if (enabled_) emitDepth(out, updateContext());
  }

  // B6 centers pitch but retains the depth for the next B4.
  void stop(core::PerformanceEmitter out) {
    enabled_ = false;
    out.vibratoDepth(0.0, updateContext());
  }

  // E4 interpolates the stored period in musical ticks, not frequency in Hz.
  void fadeRate(u16 ticks, u8 target) {
    period_.begin(core::SequenceMotionPlan<double>::targetOverTicks(std::max<u8>(1, target), ticks));
  }

  void tick(core::PerformanceEmitter out) {
    period_.tickChanged([&](double) { out.vibratoRate(frequency(), updateContext()); });
  }

private:
  bool enabled_ = false;
  u8 depth_ = 0;
  double cycleFrequencyHz_ = 0;
  core::SequenceLinearMotion<double> period_{256.0};

  [[nodiscard]] double frequency() const { return cycleFrequencyHz_ / period_.current(); }

  // Later commands retain the waveform, delay, phase, and depth buildup set by B4.
  [[nodiscard]] core::LfoPerformanceContext updateContext() const {
    return {.sampleImmediatelyOnNote = true, .phaseRunsAtZeroDepth = enabled_};
  }

  // The high bit selects wide depth. The driver applies twice as much pitch
  // change upwards as downwards; convert those endpoints to semitones.
  void emitDepth(core::PerformanceEmitter out, core::LfoPerformanceContext context) const {
    const double amount = (depth_ & 0x7f) / 128.0 * ((depth_ & 0x80) ? 1.0 : 15.0 / 256.0);
    const double down = 12.0 * std::log2(1.0 - amount / 2.0);
    const double up = 12.0 * std::log2(1.0 + amount);
    context.pitchRangeSemitones = core::ModulationRange{down, up};
    out.vibratoDepth(std::max(-down, up), std::move(context));
  }
};

}  // namespace vgmtrans::formats::akao
