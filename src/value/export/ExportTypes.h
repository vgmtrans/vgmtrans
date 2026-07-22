/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"
#include "value/export/ExportPolicy.h"

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

struct MidiExportOptions {
  // Auto follows neutral source quantization when available, then falls back
  // to the legacy precision hint used by unmigrated dialects.
  MidiLevelResolution volumeResolution = MidiLevelResolution::Auto;
  MidiLevelResolution expressionResolution = MidiLevelResolution::Auto;
  bool skipChannel10 = true;
  bool writePortMetaEvents = true;
  MidiBankSelectStyle bankSelectStyle = MidiBankSelectStyle::MsbOnly;
};

// Policy for exporting one sequence as a self-contained Standard MIDI file.
// Synth modulation is rendered into MIDI events because there is no companion
// instrument container in a standalone export.
struct SequenceExportRequest {
  LoopPolicy loopPolicy = LoopPolicy::Default;
  u32 sequenceLoops = 1;
  MidiExportOptions midi;
};

// ExportRequest is policy, not parsed data. Callers choose which files to write
// and how to handle loops, MIDI channels, and modulation scaling.
struct ExportRequest {
  std::vector<ExportKind> kinds;
  LoopPolicy loopPolicy = LoopPolicy::Default;
  // Extra repeats after the initial playthrough. This is the user-facing
  // "Sequence Loops" setting: 0 means stop at the first infinite-loop point.
  u32 sequenceLoops = 1;
  MidiExportOptions midi;
  ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange;
  ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SequenceEventSimulation;
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
