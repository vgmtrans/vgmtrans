/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/synth/SynthModel.h"

#include <span>
#include <unordered_map>
#include <vector>

namespace vgmtrans::core {

struct PreparedInstrumentRef {
  u32 set = invalidIdValue;
  u32 instrument = invalidIdValue;

  [[nodiscard]] bool valid() const noexcept { return set != invalidIdValue && instrument != invalidIdValue; }

  friend bool operator==(const PreparedInstrumentRef&, const PreparedInstrumentRef&) noexcept = default;
};

struct PlannedInstrumentSelection {
  PreparedInstrumentRef instrument;
  InstrumentAddress address;
  std::optional<u16> pitchBendRangeCents;
};

struct NoteInstrumentAssignment {
  TrackId track;
  PerformanceNoteId note;
  PlannedInstrumentSelection selection;
};

struct DynamicEnvelopeMaterialization;

class SequenceInstrumentPlan {
public:
  [[nodiscard]] const PlannedInstrumentSelection* selectionFor(TrackId track, PerformanceNoteId note) const;
  [[nodiscard]] std::span<const NoteInstrumentAssignment> assignments() const noexcept;
  [[nodiscard]] bool uses(PreparedInstrumentRef instrument) const;
  [[nodiscard]] bool complete() const noexcept;

private:
  friend DynamicEnvelopeMaterialization materializeDynamicEnvelopes(
      const PerformanceSequence& performance, std::span<InstrumentSetAsset> instrumentSets);

  void assign(NoteInstrumentAssignment assignment);

  std::vector<NoteInstrumentAssignment> assignments_;
  std::unordered_map<u64, size_t> indexes_;
  bool complete_ = true;
};

struct DynamicEnvelopeMaterialization {
  SequenceInstrumentPlan instruments;
  std::vector<Diagnostic> diagnostics;
  u32 variantCount = 0;
};

// Materializes only envelopes used by fresh note attacks. The returned plan is
// shared by MIDI and synth exporters, so both select the same generated preset.
[[nodiscard]] DynamicEnvelopeMaterialization materializeDynamicEnvelopes(const PerformanceSequence& performance,
                                                                         std::span<InstrumentSetAsset> instrumentSets);

}  // namespace vgmtrans::core
