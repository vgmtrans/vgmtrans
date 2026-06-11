/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MetadataModel.h"
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

// ExportRequest is policy, not parsed data. Callers choose which containers to
// write and how to lower ambiguous behavior such as loops or modulation ranges.
struct ExportRequest {
  std::vector<ExportKind> kinds;
  LoopPolicy loopPolicy = LoopPolicy::Default;
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
