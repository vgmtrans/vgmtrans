/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class VGMColl;
class MidiFile;

namespace conversion {

enum class MidiMergeMode : uint8_t {
  Sequential
};

struct MidiMergeEntry {
  const VGMColl* collection = nullptr;
  std::string label;
};

struct MidiMergeOptions {
  MidiMergeMode mode = MidiMergeMode::Sequential;
  uint32_t sequentialGapTicks = 0;
  std::vector<uint32_t> startTimes;
  std::vector<uint8_t> bankOffsets;
};

struct MidiMergeResult {
  uint16_t ppqn = 0;
  std::vector<uint32_t> startTimes;
  std::vector<uint8_t> bankOffsets;
};

std::unique_ptr<MidiFile> mergeMidiSequences(const std::vector<MidiMergeEntry>& entries,
                                             const MidiMergeOptions& options = {},
                                             MidiMergeResult* result = nullptr);

bool planChunkBankOffsets(const std::vector<MidiMergeEntry>& entries,
                          std::vector<uint8_t>& bankOffsets);

bool saveMergedSoundfont(const std::vector<MidiMergeEntry>& entries,
                         const std::vector<uint8_t>& bankOffsets,
                         const std::filesystem::path& filepath);

}  // namespace conversion
