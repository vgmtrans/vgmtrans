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

namespace vgmtrans::formats::akao {

// Translate driver settings into the shared pitch LFO. Standard waveforms
// approximate the native tables; SPU pitch rounding is intentionally omitted.
class AkaoVibrato {
public:
  // B4 selects timing and waveform, enables vibrato, and retains B5's depth.
  void start(core::PerformanceEmitter out, AkaoProfile profile, u8 delay, u16 period, u8 type) {
    using namespace core;
    enabled_ = true;
    clockHz_ = profile.driverTickHz();
    period_.reset(period);
    context_ = {
        .delay = LfoDelay{.ticks = delay, .tempoRelative = true},
        .shape = LfoShape{},
        .polarity = LfoPolarity::Bipolar,
        .initialPhaseCycles = 0.0,
        .steppedDepthAttackSteps = 0,
        .sampleImmediatelyOnNote = true,
        .restartMode = LfoRestartMode::PhaseAndDelay,
        .phaseRunsAtZeroDepth = true,
    };
    auto& shape = *context_.shape;
    if (profile.version32()) {
      // Unlike earlier drivers, B4 retains the current waveform position.
      context_.restartMode = LfoRestartMode::Delay;
      // V3.2 measures a quarter sine/triangle cycle or half a square cycle.
      // Types 4..7 add a four-step depth buildup to types 0..3.
      type &= 7;
      constexpr LfoWaveform waves[]{LfoWaveform::Square, LfoWaveform::Sine,
                                    LfoWaveform::Triangle, LfoWaveform::Noise};
      shape.waveform = waves[type & 3];
      cycleSteps_ = (type & 3) == 0 ? 2 : 4;
      context_.steppedDepthAttackSteps = type >= 4 ? 4 : 0;
      if ((type & 3) == 3) {
        // The random table has half the other waveforms' depth. Generate a
        // repeatable substitute; type 7 increases depth every two table passes.
        u32 noise = 1;
        for (u32 i = 0; i < 256; ++i) {
          noise = noise * 1664525u + 1013904223u;
          shape.samples.push_back(static_cast<double>(noise >> 24) / 256.0 - 0.5);
        }
        cycleSteps_ = type == 7 ? 2 : 1;
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
      cycleSteps_ = lengths[type];
      shape.waveform = LfoWaveform::Sine;
      if (type < 6) shape.waveform = LfoWaveform::Square;
      else if (type == 8 || type == 9) shape.waveform = LfoWaveform::Triangle;
      else if (type == 12 || type == 13) shape.waveform = LfoWaveform::Noise;
      context_.steppedDepthAttackSteps = type != 12 && type % 2 == 0 ? 4 : 0;
      if (type == 2 || type == 3) context_.polarity = LfoPolarity::Positive;
      if (type == 4 || type == 5) {
        context_.polarity = LfoPolarity::Negative;
        context_.initialPhaseCycles = 0.5;
      }
    }
    context_.frequencyHz = frequency();
    emitDepth(out);
    context_.restartMode = LfoRestartMode::None;
    context_.shape.reset();
  }

  // B5 changes depth without enabling vibrato or restarting phase and delay.
  void setDepth(core::PerformanceEmitter out, u8 value) {
    depth_ = value;
    if (enabled_) emitDepth(out);
  }

  // B6 centers pitch but retains the depth for the next B4.
  void stop(core::PerformanceEmitter out) {
    enabled_ = false;
    context_.phaseRunsAtZeroDepth = false;
    out.vibratoDepth(0.0, context_);
  }

  // E4 interpolates the stored period in musical ticks, not frequency in Hz.
  void fadeRate(u16 ticks, u8 target) {
    period_.begin(core::SequenceMotionPlan<double>::targetOverTicks(std::max<u8>(1, target), ticks));
  }

  void tick(core::PerformanceEmitter out) {
    period_.tickChanged([&](double) {
      context_.frequencyHz = frequency();
      out.vibratoRate(*context_.frequencyHz, context_);
    });
  }

private:
  bool enabled_ = false;
  u8 depth_ = 0;
  double clockHz_ = 0;
  double cycleSteps_ = 1;
  core::SequenceLinearMotion<double> period_{256.0};
  core::LfoPerformanceContext context_;

  [[nodiscard]] double frequency() const { return clockHz_ / (period_.current() * cycleSteps_); }

  // The high bit selects wide depth. The driver applies twice as much pitch
  // change upwards as downwards; convert those endpoints to semitones.
  void emitDepth(core::PerformanceEmitter out) {
    const double amount = (depth_ & 0x7f) / 128.0 * ((depth_ & 0x80) ? 1.0 : 15.0 / 256.0);
    const double down = 12.0 * std::log2(1.0 - amount / 2.0);
    const double up = 12.0 * std::log2(1.0 + amount);
    context_.pitchRangeSemitones = core::ModulationRange{down, up};
    out.vibratoDepth(std::max(-down, up), context_);
  }
};

}  // namespace vgmtrans::formats::akao
