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
  // Auto follows the source precision hints recorded in PerformanceSequence.
  MidiLevelResolution volumeResolution = MidiLevelResolution::Auto;
  MidiLevelResolution expressionResolution = MidiLevelResolution::Auto;
  bool skipChannel10 = true;
  MidiBankSelectStyle bankSelectStyle = MidiBankSelectStyle::MsbOnly;
};

// ExportRequest is policy, not parsed data. Callers choose which containers to
// write and how to lower ambiguous behavior such as loops or modulation ranges.
struct ExportRequest {
  std::vector<ExportKind> kinds;
  LoopPolicy loopPolicy = LoopPolicy::Default;
  // Extra repeats after the initial playthrough. This mirrors the legacy
  // "Sequence Loops" setting: 0 means stop at the first infinite-loop point.
  u32 sequenceLoops = 0;
  MidiExportOptions midi;
  ModulationScalingPolicy synthModulationScaling = ModulationScalingPolicy::FullFormatRange;
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
