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
  // Stores the actual values seen in a sequence. Synth exporters use this to avoid
  // mapping a tiny real vibrato range across the full theoretical controller range.
  bool observed = false;
  u32 min = 0;
  u32 max = 0;
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
  // Controller-level usage reflects the MIDI controller range consumed by SF2/DLS
  // modulators. Prefer deriving it from PerformanceSequence when possible so source
  // driver semantics stay above MIDI rendering.
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
