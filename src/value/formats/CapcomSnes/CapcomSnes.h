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

#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

inline constexpr u64 kCapcomSnesAramSize = 0x10000;
inline constexpr u32 kCapcomSnesMaxTracks = 8;
inline constexpr u32 kCapcomSnesPpqn = 48;
inline constexpr double kCapcomSnesLfoStepHertz = 1000.0 / 16384.0;
inline constexpr std::string_view kCapcomSnesInstrumentDomain = "capcom-snes.instrument";

enum class CapcomSnesEngineVersion : u8 {
  none,
  v1BgmInList,
  v2BgmUsuallyAtFixedLocation,
  v3BgmFixedLocation,
};

struct CapcomSnesLayout {
  // Capcom SPC snapshots have no declarative layout. These final locations are
  // recovered from driver-code signatures before any asset is parsed.
  CapcomSnesEngineVersion version = CapcomSnesEngineVersion::none;
  core::SourceRange sequenceHeaderRange;
  u32 trackPointerTableAddress = 0;
  std::optional<u32> instrumentTableAddress;
  std::optional<u32> spcDirAddress;
};

struct CapcomSnesInstrumentInfo {
  u32 index = 0;
  u8 srcn = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  s16 pitchScale = 0;
  core::SourceRecord source;
};

[[nodiscard]] std::optional<CapcomSnesLayout> findCapcomSnesLayout(core::ByteReader reader);

[[nodiscard]] const core::SequenceProgramConfig& capcomSnesSequenceConfig();
[[nodiscard]] core::SequenceRuntime capcomSnesSequenceRuntime(CapcomSnesEngineVersion version);

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
                                                             core::AssetId sequenceId,
                                                             core::SourceMapBuilder* sourceMap = nullptr,
                                                             std::vector<core::Diagnostic>* diagnostics = nullptr);

[[nodiscard]] std::vector<CapcomSnesInstrumentInfo> parseCapcomSnesInstrumentInfos(core::ByteReader reader,
                                                                                   u32 instrumentTableAddress,
                                                                                   u32 spcDirAddress);
[[nodiscard]] std::optional<core::ScanSynthRefs> addCapcomSnesSynth(core::ScanResultBuilder& builder,
                                                                    u32 instrumentTableAddress, u32 spcDirAddress,
                                                                    std::string_view displayName);

[[nodiscard]] core::FormatModule capcomSnesModule();

}  // namespace vgmtrans::formats::capcom_snes
