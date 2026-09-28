/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/synth/SynthModel.h"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace vgmtrans::core {

struct InstrumentPreparationOptions {
  bool dynamicEnvelopes = false;
  bool signedStereo = false;
  bool onlyUsedInstruments = false;
  // Start an independent stitched part's bank namespace here.
  std::optional<u32> firstBank;
};

// An instrument definition and its output address. Null means an external
// preset. The definition is borrowed; its owning bank must outlive conversion.
struct ResolvedInstrument {
  const Instrument* instrument = nullptr;
  InstrumentAddress address;
};

struct SoundingVoice {
  // Always a handle or an external address after preparation.
  InstrumentSelection instrument;
  std::optional<u64> endLimit;  // Absolute tick; computed with the source tempo map.
};

struct MidiExportOptions;

// Preparation resolves the source's final note/continuation bindings into voices.
// Its segments share an adapted instrument and a hardware stop deadline. Bank
// copies and output addresses are owned and frozen together. MIDI lowering
// copies events and addresses but shares the banks. Address exhaustion retains
// the prepared data and diagnostics, but prevents MIDI/synth output.
class ResolvedPerformance {
public:
  [[nodiscard]] const PerformanceSequence& performance() const noexcept { return performance_; }
  [[nodiscard]] const std::vector<SoundBankAsset>& soundBanks() const noexcept { return *soundBanks_; }
  [[nodiscard]] const Instrument* initialInstrument() const noexcept { return initialInstrument_; }
  // Notes, changes and handles must belong to this prepared performance.
  [[nodiscard]] ResolvedInstrument selectionFor(InstrumentHandle handle) const;
  [[nodiscard]] ResolvedInstrument selectionFor(const NotePerformanceEvent& note) const;
  [[nodiscard]] ResolvedInstrument selectionFor(const InstrumentPerformanceEvent& change) const;
  [[nodiscard]] const SoundingVoice& voiceFor(const NotePerformanceEvent& note) const { return voices_.at(note.voice.value); }
  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] bool onlyUsedInstruments() const noexcept { return onlyUsedInstruments_; }
  [[nodiscard]] const std::map<InstrumentHandle, InstrumentAddress>& instrumentAddresses() const { return addresses_; }
  [[nodiscard]] const std::map<u32, u32>& bankMapping() const { return banks_; }
  [[nodiscard]] u32 nextBank() const;
  [[nodiscard]] std::set<InstrumentHandle> usedInstruments() const;

private:
  friend class PreparedCollection;
  friend ResolvedPerformance preparePerformance(PerformanceSequence, std::vector<SoundBankAsset>,
                                                 InstrumentPreparationOptions);
  friend ResolvedPerformance lowerMidiPerformanceAutomation(ResolvedPerformance, const MidiExportOptions&,
                                                            const PerformanceTempoMap&);
  ResolvedPerformance(PerformanceSequence performance, std::vector<SoundBankAsset> soundBanks,
                      InstrumentSelection initialInstrument, std::vector<SoundingVoice> voices,
                      const InstrumentPreparationOptions& options);
  void assignAddresses(const InstrumentPreparationOptions& options);
  [[nodiscard]] ResolvedInstrument selectedInstrument(const InstrumentSelection& selection) const;

  PerformanceSequence performance_;
  std::vector<SoundingVoice> voices_;
  std::shared_ptr<const std::vector<SoundBankAsset>> soundBanks_;
  const Instrument* initialInstrument_ = nullptr;  // Points into the shared immutable banks.
  std::map<InstrumentHandle, InstrumentAddress> addresses_;
  std::map<u32, u32> banks_;
  bool onlyUsedInstruments_;
  bool valid_ = true;
};

// The only source-selection resolver. Exact identity first, numeric fallback,
// first matching definition in bank order; fallbacks/conflicts are diagnosed.
// Resolution and variant creation share one chronological pass. Source notes
// are resolved only against original definitions, never newly generated variants.
[[nodiscard]] ResolvedPerformance preparePerformance(
    PerformanceSequence performance, std::vector<SoundBankAsset> soundBanks = {},
    InstrumentPreparationOptions options = {});

}  // namespace vgmtrans::core
