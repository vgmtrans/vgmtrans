/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"
#include "value/export/ExportPolicy.h"
#include "value/synth/SampleFiltering.h"

#include <string>
#include <vector>

namespace vgmtrans::core {

enum class ExportKind {
  Midi,
  SoundFont2,
  Dls,
  Wav,
};

enum class SynthExportFormat {
  SoundFont2,
  Dls,
};

enum class MidiLevelResolution {
  Auto,
  SevenBit,
  FourteenBit,
};

enum class MidiBankSelectStyle {
  MsbOnly,
  MsbAndLsb,
};

enum class MidiPitchTransitionRendering {
  // Use each transition's format-provided compatibility preference.
  PreserveFormat,
  Portamento,
  PitchBend,
};

enum class MidiWideTuningRendering {
  // Preserve sampler key selection on receivers that apply Coarse Tune before
  // choosing an instrument region.
  PitchBend,
  CoarseTune,
};

struct MidiExportOptions {
  // Auto follows neutral source quantization when available, then falls back
  // to the legacy precision hint used by unmigrated formats.
  MidiLevelResolution volumeResolution = MidiLevelResolution::Auto;
  MidiLevelResolution expressionResolution = MidiLevelResolution::Auto;
  bool skipChannel10 = true;
  bool writePortMetaEvents = true;
  MidiBankSelectStyle bankSelectStyle = MidiBankSelectStyle::MsbOnly;
  MidiPitchTransitionRendering pitchTransitions = MidiPitchTransitionRendering::PreserveFormat;
  MidiWideTuningRendering wideTuning = MidiWideTuningRendering::PitchBend;
  // Approximate a source track backed by one physical voice: after the first
  // attack, terminate any lingering sound on its MIDI channel before the next.
  bool terminatePreviousVoice = false;
};

// Options shared by standalone sequence export, collection export, and
// collection playback.
struct SequenceRenderOptions {
  LoopPolicy loopPolicy = LoopPolicy::Default;
  // Extra repeats after the initial playthrough. This is the user-facing
  // "Sequence Loops" setting: 0 means stop at the first infinite-loop point.
  u32 sequenceLoops = 1;
  MidiExportOptions midi;
};

// Standalone sequence export simulates synth modulation in MIDI because there
// is no companion instrument container.
using SequenceExportRequest = SequenceRenderOptions;

// A playback backend selects how the rendered MIDI and companion synth divide
// modulation work. The value core honors that policy without knowing which
// backend will consume the prepared data.
struct PlaybackRequest {
  SequenceRenderOptions sequence;
  ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators;
  DynamicEnvelopePolicy dynamicEnvelopes = DynamicEnvelopePolicy::Ignore;
  SampleFilteringPolicy sampleFiltering = SampleFilteringPolicy::FormatPreferred;
};

// ExportRequest is policy, not parsed data. Callers choose which files to write
// and how to handle loops, MIDI channels, and modulation scaling.
struct ExportRequest {
  std::vector<ExportKind> kinds;
  SequenceRenderOptions sequence;
  ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange;
  ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators;
  DynamicEnvelopePolicy dynamicEnvelopes = DynamicEnvelopePolicy::Ignore;
  SampleFilteringPolicy sampleFiltering = SampleFilteringPolicy::FormatPreferred;
  // Instrument-container exports retain only instruments selected by rendered
  // notes, plus the samples referenced by those instruments.
  bool exportOnlyUsedInstruments = false;
};

// Artifact carries diagnostics even when no bytes were produced, so UI/CLI
// callers can report partial export failures without losing context.
struct Artifact {
  std::string filename;
  std::string mediaType;
  std::vector<u8> bytes;
  std::vector<Diagnostic> diagnostics;
};

// One collection can produce several related files, for example MIDI plus a
// SoundFont/DLS instrument container.
struct CollectionExport {
  CollectionId collection;
  std::vector<Artifact> artifacts;
};

}  // namespace vgmtrans::core
