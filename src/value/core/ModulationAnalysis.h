/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MidiModel.h"
#include "value/core/SequenceModel.h"

#include <optional>
#include <vector>

namespace vgmtrans::core {

struct ObservedValueRange {
  // Stores the actual values seen in a sequence. Synth exporters use this to avoid
  // mapping a tiny real vibrato range across the full theoretical controller range.
  bool observed = false;
  u32 min = 0;
  u32 max = 0;
  std::optional<SourceRange> firstRange;
};

struct TrackModulationUsage {
  u32 sourceTrackNumber = 0;
  ObservedValueRange vibratoDepth;
  ObservedValueRange tremoloDepth;
  ObservedValueRange modulationRate;
};

struct ModulationUsage {
  // Command-level usage keeps source ranges, useful for diagnostics and future format
  // decisions before MIDI lowering has quantized values.
  ObservedValueRange vibratoDepth;
  ObservedValueRange tremoloDepth;
  ObservedValueRange modulationRate;
  std::vector<TrackModulationUsage> tracks;
};

struct MidiTrackModulationUsage {
  u32 trackIndex = 0;
  ObservedValueRange vibratoDepth;
  ObservedValueRange vibratoRate;
  ObservedValueRange tremoloDepth;
  ObservedValueRange tremoloRate;
};

struct MidiModulationUsage {
  // MIDI-level usage reflects the final controller values that will be written to a MIDI
  // file and consumed by SF2/DLS modulators.
  ObservedValueRange vibratoDepth;
  ObservedValueRange vibratoRate;
  ObservedValueRange tremoloDepth;
  ObservedValueRange tremoloRate;
  std::vector<MidiTrackModulationUsage> tracks;
};

[[nodiscard]] bool hasObservedValue(const ObservedValueRange& range) noexcept;
[[nodiscard]] bool hasModulationUsage(const TrackModulationUsage& usage) noexcept;
[[nodiscard]] bool hasModulationUsage(const ModulationUsage& usage) noexcept;
[[nodiscard]] bool hasMidiModulationUsage(const MidiTrackModulationUsage& usage) noexcept;
[[nodiscard]] bool hasMidiModulationUsage(const MidiModulationUsage& usage) noexcept;
[[nodiscard]] ModulationUsage analyzeModulationUsage(const CommandSequence& sequence);
[[nodiscard]] MidiModulationUsage analyzeMidiModulationUsage(const MidiSequence& sequence);

}  // namespace vgmtrans::core
