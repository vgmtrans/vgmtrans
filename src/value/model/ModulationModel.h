/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <optional>

namespace vgmtrans::core {

struct ModulationRange {
  double minimum = 0.0;
  double maximum = 0.0;
};

enum class LfoWaveform {
  Sine,
  Triangle,
  Square,
  SawtoothUp,
  SawtoothDown,
  Noise,
};

enum class LfoPolarity {
  Bipolar,
  Positive,
  Negative,
};

enum class ModulationDepthMode {
  // A sequence controller selects a value from zero through maxDepth.
  Controller,
  // The hardware voice applies maxDepth continuously.
  Fixed,
};

enum class TremoloGainMode {
  // The synth LFO is centered around nominal gain and may boost above it.
  BipolarAroundNominal,
  // Matching attenuation keeps the loudest point at nominal gain.
  NoBoost,
};

struct VibratoSpec {
  double maxDepthCents = 0.0;
  ModulationRange rateHertz;
  std::optional<LfoWaveform> waveform;
  std::optional<ModulationRange> delaySeconds;
  ModulationDepthMode depthMode = ModulationDepthMode::Controller;
};

struct TremoloSpec {
  double maxDepthDb = 0.0;
  ModulationRange rateHertz;
  std::optional<LfoWaveform> waveform;
  TremoloGainMode gainMode = TremoloGainMode::BipolarAroundNominal;
  std::optional<ModulationRange> delaySeconds;
  ModulationDepthMode depthMode = ModulationDepthMode::Controller;
};

// Physical vibrato and tremolo, whether static or derived from a sequence.
// Exporters lower these values to target-specific records.
struct InstrumentModulation {
  std::optional<VibratoSpec> vibrato;
  std::optional<TremoloSpec> tremolo;
};

}  // namespace vgmtrans::core
