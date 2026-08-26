/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"

#include <optional>

namespace vgmtrans::core {

struct SequenceModulationProfile;

struct MidiModulationMaximum {
  // MIDI scaling uses the quantized controller maximum; synth scaling uses
  // the precise normalized amount that produced it.
  u8 controllerValue = 0;
  double normalized = 0.0;
};

struct MidiModulationUsage {
  // Aggregate maxima for the whole sequence. Analyze PerformanceSequence so
  // source meaning is read before MIDI quantization.
  std::optional<MidiModulationMaximum> vibratoDepth;
  std::optional<MidiModulationMaximum> vibratoRate;
  std::optional<MidiModulationMaximum> tremoloDepth;
  std::optional<MidiModulationMaximum> tremoloRate;
};

[[nodiscard]] bool hasMidiModulationUsage(const MidiModulationUsage& usage) noexcept;
[[nodiscard]] MidiModulationUsage analyzePerformanceModulationUsage(
    const PerformanceSequence& sequence, const SequenceModulationProfile* modulationProfile = nullptr);

}  // namespace vgmtrans::core
