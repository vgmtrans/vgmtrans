/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/scan/FormatDefinition.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceDialect.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::nds {

inline constexpr std::string_view kNdsFormatName = "NDS";
inline constexpr auto kNdsSequenceDialectId = "nds:sseq";

// SDAT container locations and table-of-contents entries.
struct NdsFileRange {
  u32 offset = 0;
  u32 size = 0;
};

struct NdsSequenceRange {
  u32 offset = 0;
  u32 sequenceEnd = 0;
  bool recoverMalformedSdatRange = false;
};

struct NdsSequenceInfo {
  bool valid = false;
  u16 fileId = 0xffff;
  u16 bank = 0xffff;
};

struct NdsBankInfo {
  bool valid = false;
  u16 fileId = 0xffff;
  std::array<u16, 4> waveArchives{0xffff, 0xffff, 0xffff, 0xffff};
};

struct NdsWaveArchiveInfo {
  bool valid = false;
  u16 fileId = 0xffff;
};

struct NdsLayout {
  // Parsed SDAT table-of-contents. File IDs refer into FAT; sequence/bank/wave indexes
  // refer into INFO/SYMB tables.
  u32 baseOffset = 0;
  u32 length = 0;
  u32 symbOffset = 0;
  u32 infoOffset = 0;
  u32 fatOffset = 0;
  bool hasSymb = false;
  std::vector<std::string> sequenceNames;
  std::vector<std::string> bankNames;
  std::vector<std::string> waveArchiveNames;
  std::vector<NdsSequenceInfo> sequences;
  std::vector<NdsBankInfo> banks;
  std::vector<NdsWaveArchiveInfo> waveArchives;
};

// SDAT discovery and bounded subfile ranges.
[[nodiscard]] std::vector<u32> findNdsSdatOffsets(core::ByteReader reader);
[[nodiscard]] std::optional<NdsLayout> parseNdsLayout(core::ByteReader reader, u32 baseOffset);
[[nodiscard]] std::optional<NdsFileRange> ndsFileRange(core::ByteReader reader, const NdsLayout& layout, u16 fileId);
[[nodiscard]] NdsSequenceRange ndsSequenceRangeForFatEntry(core::ByteReader reader, u32 offset, u32 size);

// SSEQ bytecode decoding and playback semantics.
[[nodiscard]] const core::SequenceDialect& ndsSequenceDialect();
[[nodiscard]] core::SequenceProgramAsset parseNdsSequenceProgram(const core::ScanInput& input, core::AssetId id,
                                                                 NdsSequenceRange range, const std::string& name,
                                                                 core::SourceMapBuilder* sourceMap = nullptr,
                                                                 std::vector<core::Diagnostic>* diagnostics = nullptr);

// SBNK instruments and SWAR/PSG samples. These functions add complete synth
// assets directly to the scan result and return the handles used by collections.
[[nodiscard]] core::ScanSampleCollectionRef addNdsPsgSamples(core::ScanResultBuilder& builder);
[[nodiscard]] std::optional<core::ScanSampleCollectionRef> addNdsWaveArchive(core::ScanResultBuilder& builder,
                                                                             NdsFileRange range, std::string_view name);
[[nodiscard]] std::optional<core::ScanInstrumentSetRef> addNdsInstrumentSet(
    core::ScanResultBuilder& builder, NdsFileRange range, std::string_view name,
    core::ScanSampleCollectionRef psgCollection,
    const std::array<std::optional<core::ScanSampleCollectionRef>, 4>& waveCollections);

[[nodiscard]] core::FormatDefinition ndsDefinition();

}  // namespace vgmtrans::formats::nds
