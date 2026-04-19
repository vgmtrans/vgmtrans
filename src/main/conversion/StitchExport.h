/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <filesystem>
#include <vector>

#include "MidiMerge.h"

namespace conversion {

struct StitchedExportResult {
  MidiMergeResult mergeResult;
  std::vector<uint8_t> bankOffsets;
};

bool exportStitchedMidiAndSf2(const std::vector<MidiMergeEntry> &entries,
                              const std::filesystem::path &midiPath,
                              const std::filesystem::path &sf2Path,
                              StitchedExportResult *result = nullptr);

}  // namespace conversion
