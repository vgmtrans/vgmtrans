/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/synth/SampleFiltering.h"
#include "value/synth/SynthModel.h"

#include <span>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct PerformanceSequence;
class FormatRegistry;

struct SynthExportInput {
  std::string name;
  std::span<const InstrumentSetAsset* const> instrumentSets;
  std::span<const SampleCollectionAsset* const> sampleCollections;
  // Resolves FormatPreferred independently for each sample collection.
  const FormatRegistry* formats = nullptr;
  // Null retains every instrument. A performance selects the instruments used
  // by its notes; sample filtering can also be requested independently.
  const PerformanceSequence* sequenceUsage = nullptr;
  bool filterSamplesToReferencedInstruments = false;
  const MidiModulationUsage* midiModulationUsage = nullptr;
  ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange;
  ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators;
  SampleFilteringPolicy sampleFiltering = SampleFilteringPolicy::FormatPreferred;
};

struct SynthExportResult {
  std::vector<u8> bytes;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] SynthExportResult buildDls(const SynthExportInput& input, const SourceStore& sources);
[[nodiscard]] SynthExportResult buildSoundFont2(const SynthExportInput& input, const SourceStore& sources);

struct SynthSampleDecodeOptions {
  bool requireMono = false;
  std::string nonMonoWarning;
};

// Decoded sample plus its original collection/index identity. Synth exporters use
// this to build one flat sample table without losing region references.
struct DecodedSynthSample {
  AssetId collectionId;
  u32 localIndex = 0;
  std::string name;
  Tuning pitch;
  double attenuationDb = 0.0;
  DecodedSample decoded;
};

struct ResolvedSynthRegion {
  const Region* region = nullptr;
  u16 sampleIndex = 0;
  std::vector<SynthGenerator> generators;
  std::vector<SynthModulator> modulators;
};

struct ResolvedSynthInstrument {
  const Instrument* instrument = nullptr;
  InstrumentAddress address;
  std::vector<ResolvedSynthRegion> regions;
  std::vector<SynthGenerator> generators;
  std::vector<SynthModulator> modulators;
};

struct PreparedSynthData {
  std::vector<DecodedSynthSample> samples;
  std::vector<ResolvedSynthInstrument> instruments;
  std::vector<Diagnostic> diagnostics;
};

// Apply the same performance-based instrument selection used by SF2 and DLS
// preparation without decoding samples or lowering a container.
[[nodiscard]] std::vector<const Instrument*> selectSynthInstruments(
    std::span<const InstrumentSetAsset* const> instrumentSets, const PerformanceSequence* sequenceUsage);

// SF2 and DLS have one decay followed by a fixed sustain level. Approximate a
// richer envelope using both endpoint timing and perceptual salience, without
// changing the instrument data kept by the scanner. attenuationRangeDb is the
// target's full-scale volume-envelope range.
[[nodiscard]] Envelope approximateEnvelopeAsAdsr(Envelope envelope, double attenuationRangeDb = 100.0);

// Decode samples and resolve instrument references once before a format-specific
// writer lays out its container.
[[nodiscard]] PreparedSynthData prepareSynthData(const SynthExportInput& input, const SourceStore& sources,
                                                 const SynthSampleDecodeOptions& options = {});

}  // namespace vgmtrans::core
