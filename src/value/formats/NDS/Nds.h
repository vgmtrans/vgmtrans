/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::nds {

inline constexpr std::string_view kNdsFormatName = "NDS";

struct NdsSequenceRange {
  u32 offset = 0;
  u32 sequenceEnd = 0;
  bool recoverMalformedSdatRange = false;
};

struct NdsSequenceInfo {
  std::string name;
  std::optional<core::SourceRange> file;
  std::optional<u16> bank;
};

struct NdsBankInfo {
  std::string name;
  std::optional<core::SourceRange> file;
  std::array<std::optional<u16>, 4> waveArchives;
};

struct NdsWaveArchiveInfo {
  std::string name;
  std::optional<core::SourceRange> file;
};

struct NdsLayout {
  // Names and FAT ranges are resolved here so the module only needs to follow
  // sequence -> bank -> wave-archive relationships.
  core::SourceRange range;
  std::vector<NdsSequenceInfo> sequences;
  std::vector<NdsBankInfo> banks;
  std::vector<NdsWaveArchiveInfo> waveArchives;
};

// SDAT discovery and layout parsing.
[[nodiscard]] std::vector<u32> findNdsSdatOffsets(core::ByteReader reader);
[[nodiscard]] std::optional<NdsLayout> parseNdsLayout(core::ScanResultBuilder& builder, u32 baseOffset);
[[nodiscard]] NdsSequenceRange ndsSequenceRangeForFatEntry(core::ByteReader reader, core::SourceRange file);

// SSEQ bytecode decoding and playback semantics.
[[nodiscard]] const core::SequenceProgramConfig& ndsSequenceConfig();
[[nodiscard]] core::SequenceRuntime ndsSequenceRuntime();
[[nodiscard]] core::SequenceProgram parseNdsSequenceProgram(core::ByteReader reader, core::AssetId id,
                                                            NdsSequenceRange range,
                                                            core::SourceMapBuilder* sourceMap = nullptr,
                                                            std::vector<core::Diagnostic>* diagnostics = nullptr);

// SBNK instruments and SWAR/PSG samples remain live drafts until the containing
// scan finishes, so sparse wave indexes stay available to later bank parsing.
[[nodiscard]] core::ScanSampleCollectionDraft addNdsPsgSamples(core::ScanResultBuilder& builder);
[[nodiscard]] std::optional<core::ScanSampleCollectionDraft> addNdsWaveArchive(core::ScanResultBuilder& builder,
                                                                               core::SourceRange range,
                                                                               std::string_view name);
[[nodiscard]] std::optional<core::ScanInstrumentSetDraft> addNdsInstrumentSet(
    core::ScanResultBuilder& builder, core::SourceRange range, std::string_view name,
    const core::ScanSampleCollectionDraft& psgCollection,
    const std::array<std::optional<core::ScanSampleCollectionDraft>, 4>& waveCollections);
[[nodiscard]] core::FormatModule ndsModule();

}  // namespace vgmtrans::formats::nds
