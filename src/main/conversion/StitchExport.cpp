/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "StitchExport.h"

#include <exception>
#include <memory>
#include <utility>

#include "LogManager.h"
#include "MidiFile.h"

namespace conversion {
namespace {

bool ensureParentPathExists(const std::filesystem::path &path) {
  try {
    if (path.has_parent_path() &&
        !std::filesystem::exists(path.parent_path())) {
      std::filesystem::create_directories(path.parent_path());
    }
  } catch (const std::exception &e) {
    L_ERROR("Failed to create output directory '{}': {}", path.parent_path().string(), e.what());
    return false;
  }

  return true;
}

}  // namespace

bool exportStitchedMidiAndSf2(const std::vector<MidiMergeEntry> &entries,
                              const std::filesystem::path &midiPath,
                              const std::filesystem::path &sf2Path,
                              StitchedExportResult *result) {
  if (entries.empty()) {
    L_ERROR("No sequences were provided for stitched export.");
    return false;
  }

  if (!ensureParentPathExists(midiPath) ||
      !ensureParentPathExists(sf2Path)) {
    return false;
  }

  std::vector<uint8_t> bankOffsets;
  if (!planChunkBankOffsets(entries, bankOffsets)) {
    L_ERROR("Failed to plan chunk bank remap.");
    return false;
  }

  MidiMergeOptions options;
  options.bankOffsets = bankOffsets;

  MidiMergeResult mergeResult;
  std::unique_ptr<MidiFile> mergedMidi =
      mergeMidiSequences(entries, options, &mergeResult);
  if (!mergedMidi) {
    L_ERROR("Failed to stitch MIDI.");
    return false;
  }

  if (!mergedMidi->saveMidiFile(midiPath)) {
    L_ERROR("Failed to write stitched MIDI: {}", midiPath.string());
    return false;
  }

  if (!saveMergedSoundfont(entries, bankOffsets, sf2Path)) {
    L_ERROR("Failed to write merged SF2: {}", sf2Path.string());
    return false;
  }

  if (result) {
    result->mergeResult = std::move(mergeResult);
    result->bankOffsets = std::move(bankOffsets);
  }

  return true;
}

}  // namespace conversion
