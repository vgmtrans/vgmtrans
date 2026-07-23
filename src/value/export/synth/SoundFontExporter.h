/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/synth/SynthExportData.h"

namespace vgmtrans::core {

// Writes resolved instrument/sample assets as an SF2 container. Sample bytes are
// decoded through SourceStore so source-backed diagnostics remain available.
class SoundFontExporter {
public:
  [[nodiscard]] SynthExportResult exportSoundFont(const SynthExportInput& input, const SourceStore& sources) const;
};

}  // namespace vgmtrans::core
