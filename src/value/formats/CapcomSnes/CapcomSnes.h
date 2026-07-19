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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

inline constexpr u64 kCapcomSnesAramSize = 0x10000;
inline constexpr u32 kCapcomSnesMaxTracks = 8;
inline constexpr u32 kCapcomSnesPpqn = 48;
inline constexpr double kCapcomSnesLfoStepHertz = 1000.0 / 16384.0;
inline constexpr s32 kCapcomSnesTremoloHalfDepthCentibels = 484;
inline constexpr std::string_view kCapcomSnesInstrumentDomain = "capcom-snes.instrument";

enum class CapcomSnesEngineVersion : u8 {
  none,
  v1BgmInList,
  v2BgmUsuallyAtFixedLocation,
  v3BgmFixedLocation,
};

struct CapcomSnesLayout {
  // Capcom SPC snapshots have no declarative layout. These addresses are
  // recovered from driver-code signatures before any asset is parsed.
  CapcomSnesEngineVersion version = CapcomSnesEngineVersion::none;
  bool hasSongList = false;
  bool bgmAtFixedAddress = false;
  u32 songListAddress = 0;
  u32 bgmHeaderAddress = 0;
  u32 sequenceHeaderAddress = 0;
  bool priorityInHeader = false;
  std::optional<u32> instrumentTableAddress;
  std::optional<u32> spcDirAddress;
};

struct CapcomSnesSampleInfo {
  u8 srcn = 0;
  u32 dirEntryAddress = 0;
  u32 startAddress = 0;
  u32 loopAddress = 0;
  u32 encodedLength = 0;
  bool loops = false;
};

struct CapcomSnesInstrumentInfo {
  u32 index = 0;
  u32 address = 0;
  u8 srcn = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  s16 pitchScale = 0;
  u32 dirEntryAddress = 0;
  u16 sampleStartAddress = 0;
  u16 sampleLoopAddress = 0;
  std::vector<core::SourceField> sourceFields;
};

[[nodiscard]] std::string capcomSnesSourceDisplayName(const core::SourceFile& source);
[[nodiscard]] std::optional<CapcomSnesLayout> findCapcomSnesLayout(core::ByteReader reader);

[[nodiscard]] const core::SequenceDialect& capcomSnesSequenceDialect();

// Focused seam for command-decoder tests. Whole-format parsing uses one shared
// TrackDecodeScope internally rather than rebuilding these values per track.
struct CapcomSnesTrackDecodeOptions {
  u32 trackIndex = 0;
  u32 startOffset = 0;
  core::SourceMapBuilder* sourceMap = nullptr;
  std::vector<core::Diagnostic>* diagnostics = nullptr;
};

[[nodiscard]] core::TrackProgram decodeCapcomSnesSourceTrack(core::ByteReader reader, CapcomSnesEngineVersion version,
                                                             CapcomSnesTrackDecodeOptions options);

[[nodiscard]] core::SequenceProgram decodeCapcomSnesSequence(core::ByteReader reader, const CapcomSnesLayout& layout,
                                                             core::AssetId sequenceId, core::SourceRange sequenceRange,
                                                             core::SourceMapBuilder* sourceMap = nullptr,
                                                             std::vector<core::Diagnostic>* diagnostics = nullptr);

[[nodiscard]] std::vector<CapcomSnesInstrumentInfo> parseCapcomSnesInstrumentInfos(core::ByteReader reader,
                                                                                   u32 instrumentTableAddress,
                                                                                   u32 spcDirAddress);
[[nodiscard]] std::vector<CapcomSnesSampleInfo> parseCapcomSnesSampleInfos(
    core::ByteReader reader, const std::vector<CapcomSnesInstrumentInfo>& instruments);
[[nodiscard]] core::SampleCollectionAsset parseCapcomSnesSamples(const core::ScanInput& input,
                                                                 core::AssetId sampleCollectionId,
                                                                 const std::vector<CapcomSnesSampleInfo>& sampleInfos,
                                                                 std::string_view displayName,
                                                                 core::SourceMapBuilder* sourceMap = nullptr);
[[nodiscard]] core::InstrumentSetAsset parseCapcomSnesInstrumentSet(
    const core::ScanInput& input, core::ScanResultBuilder& builder, core::AssetId instrumentSetId,
    core::ScanSampleCollectionRef sampleCollection, const std::vector<CapcomSnesInstrumentInfo>& instrumentInfos,
    const std::vector<CapcomSnesSampleInfo>& sampleInfos, std::string_view displayName);

[[nodiscard]] core::ScanResult scanCapcomSnes(const core::ScanInput& input);
[[nodiscard]] core::FormatDefinition capcomSnesDefinition();

}  // namespace vgmtrans::formats::capcom_snes
