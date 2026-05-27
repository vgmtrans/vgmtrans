/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "Types.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class VGMColl;
class MidiFile;

namespace conversion {

enum class MidiMergeMode : u8 {
  Sequential
};

struct MidiMergeEntry {
  const VGMColl* collection = nullptr;
  std::string label;
};

struct MidiMergeOptions {
  MidiMergeMode mode = MidiMergeMode::Sequential;
  u32 sequentialGapTicks = 0;
  std::vector<u32> startTimes;
  std::vector<u8> bankOffsets;
};

struct MidiMergeResult {
  u16 ppqn = 0;
  std::vector<u32> startTimes;
  std::vector<u8> bankOffsets;
};

std::unique_ptr<MidiFile> mergeMidiSequences(const std::vector<MidiMergeEntry>& entries,
                                             const MidiMergeOptions& options = {},
                                             MidiMergeResult* result = nullptr);

bool planChunkBankOffsets(const std::vector<MidiMergeEntry>& entries,
                          std::vector<u8>& bankOffsets);

bool saveMergedSoundfont(const std::vector<MidiMergeEntry>& entries,
                         const std::vector<u8>& bankOffsets,
                         const std::filesystem::path& filepath);

}  // namespace conversion
