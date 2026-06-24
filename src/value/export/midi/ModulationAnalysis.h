/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/midi/MidiModel.h"
#include "value/sequence/PerformanceModel.h"

#include <optional>
#include <vector>

namespace vgmtrans::core {

struct ObservedValueRange {
  // Min/max controller values that actually occur in the rendered sequence,
  // plus the normalized source amounts that produced them. MIDI controller
  // scaling uses the quantized values; synth modulator scaling uses the precise
  // normalized max when performance data provides it.
  bool observed = false;
  u32 min = 0;
  u32 max = 0;
  double normalizedMin = 0.0;
  double normalizedMax = 0.0;
  std::optional<SourceRange> firstRange;
};

struct MidiTrackModulationUsage {
  u32 trackIndex = 0;
  ObservedValueRange vibratoDepth;
  ObservedValueRange vibratoRate;
  ObservedValueRange tremoloDepth;
  ObservedValueRange tremoloRate;
};

struct MidiModulationUsage {
  // Aggregate modulation usage for the whole sequence plus per-track detail.
  // Prefer analyzing PerformanceSequence so source meaning is read before MIDI quantization.
  ObservedValueRange vibratoDepth;
  ObservedValueRange vibratoRate;
  ObservedValueRange tremoloDepth;
  ObservedValueRange tremoloRate;
  std::vector<MidiTrackModulationUsage> tracks;
};

[[nodiscard]] bool hasObservedValue(const ObservedValueRange& range) noexcept;
[[nodiscard]] bool hasMidiModulationUsage(const MidiTrackModulationUsage& usage) noexcept;
[[nodiscard]] bool hasMidiModulationUsage(const MidiModulationUsage& usage) noexcept;
[[nodiscard]] MidiModulationUsage analyzePerformanceModulationUsage(const PerformanceSequence& sequence);
[[nodiscard]] MidiModulationUsage analyzeMidiModulationUsage(const MidiSequence& sequence);

}  // namespace vgmtrans::core
