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
#include <set>
#include <span>
#include <vector>

namespace vgmtrans::core {

struct InstrumentVariantOptions {
  bool dynamicEnvelopes = false;
  bool signedStereo = false;
};

struct MidiExportOptions;

// Preparation is the only way to construct this value: every note has a resolved
// instrument and continuations retain their attack's adapted instrument. Bank
// copies are owned and frozen; MIDI lowering copies events but shares the banks.
class ResolvedPerformance {
public:
  [[nodiscard]] const PerformanceSequence& performance() const noexcept { return performance_; }
  [[nodiscard]] const std::vector<SoundBankAsset>& soundBanks() const noexcept { return *soundBanks_; }
  [[nodiscard]] std::vector<const SoundBankAsset*> soundBankView() const;
  [[nodiscard]] const InstrumentSelection& initialInstrument() const noexcept { return initialInstrument_; }

  [[nodiscard]] const Instrument* instrument(const InstrumentSelection& selection) const;
  [[nodiscard]] std::set<InstrumentHandle> usedInstruments() const;

private:
  friend ResolvedPerformance preparePerformance(PerformanceSequence, std::vector<SoundBankAsset>,
                                                 InstrumentVariantOptions);
  friend ResolvedPerformance lowerMidiPerformanceAutomation(ResolvedPerformance, const MidiExportOptions&,
                                                            const PerformanceTempoMap&);
  ResolvedPerformance(PerformanceSequence performance, std::vector<SoundBankAsset> soundBanks,
                      InstrumentSelection initialInstrument);

  PerformanceSequence performance_;
  std::shared_ptr<const std::vector<SoundBankAsset>> soundBanks_;
  InstrumentSelection initialInstrument_;
};

// The only source-selection resolver. Exact identity first, numeric fallback,
// first matching definition in bank order; fallbacks/conflicts are diagnosed.
// Resolution and variant creation share one chronological pass. Source notes
// are resolved only against original definitions, never newly generated variants.
[[nodiscard]] ResolvedPerformance preparePerformance(
    PerformanceSequence performance, std::vector<SoundBankAsset> soundBanks = {},
    InstrumentVariantOptions variants = {});

// Addresses are assigned after variants, before either MIDI or synth conversion.
// compactBanks starts an independent part's bank namespace at the given number.
// The same plan is consumed by both outputs; no finished MIDI is patched.
struct InstrumentAddressPlan {
  std::map<InstrumentHandle, InstrumentAddress> instruments;
  std::map<u32, u32> banks;
  std::vector<Diagnostic> diagnostics;
  bool valid = true;

  [[nodiscard]] InstrumentAddress address(const InstrumentSelection& selection) const;
  [[nodiscard]] u32 nextBank() const;
};

[[nodiscard]] InstrumentAddressPlan planInstrumentAddresses(
    const ResolvedPerformance& performance, bool onlyUsed = false,
    std::optional<u32> compactBanks = std::nullopt);

}  // namespace vgmtrans::core
