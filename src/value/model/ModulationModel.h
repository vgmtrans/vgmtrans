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

enum class TremoloGainMode {
  // The synth LFO is centered around nominal gain and may boost above it.
  BipolarAroundNominal,
  // Matching attenuation keeps the loudest point at nominal gain.
  NoBoost,
};

struct VibratoSpec {
  double maxDepthCents = 0.0;
  ModulationRange rateHertz;
  std::optional<ModulationRange> delaySeconds;
};

struct TremoloSpec {
  double maxDepthDb = 0.0;
  ModulationRange rateHertz;
  TremoloGainMode gainMode = TremoloGainMode::BipolarAroundNominal;
  std::optional<ModulationRange> delaySeconds;
};

// Physical vibrato and tremolo, whether static or derived from a sequence.
// Exporters lower these values to target-specific records.
struct InstrumentModulation {
  std::optional<VibratoSpec> vibrato;
  std::optional<TremoloSpec> tremolo;
};

}  // namespace vgmtrans::core
