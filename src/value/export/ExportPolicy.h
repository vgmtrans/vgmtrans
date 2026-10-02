/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

namespace vgmtrans::core {

// Controls whether synth modulators keep the full theoretical format range or
// are scaled to the controller values actually observed in the parsed sequence.
enum class ModulationScalingPolicy {
  FullFormatRange,
  ObservedSequenceRange,
};

// Controls how sequence-driven vibrato and tremolo are represented at export.
// SynthModulators writes controller-driven synth LFO settings for SF2/DLS-style output.
// SequenceEventSimulation collapses modulation into performance/MIDI event streams.
enum class ModulationConversionPolicy {
  SynthModulators,
  SequenceEventSimulation,
};

// Dynamic envelope commands remain in the neutral performance model regardless
// of export policy. InstrumentVariants opts into portable bank/program
// materialization for MIDI plus SF2/DLS-style playback.
enum class DynamicEnvelopePolicy {
  Ignore,
  InstrumentVariants,
};

}  // namespace vgmtrans::core
