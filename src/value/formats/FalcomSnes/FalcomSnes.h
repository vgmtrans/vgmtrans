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
#include <set>
#include <span>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::falcom_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr std::string_view kInstrumentDomain = "falcom-snes.instrument";

struct Layout {
  u16 sequenceHeaderAddress;
  u16 instrumentTableAddress;
  u16 instrumentSrcnMapAddress;
  u16 spcDirAddress;
  std::array<std::optional<u16>, kTrackCount> trackStarts;
};

struct SequenceParse {
  core::SequenceProgram program;
  std::set<u8> programs;
  core::SourceRange headerRange;
};

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, u32 trackNumber, u32 startAddress,
                                                   std::span<const u8, 7> durations,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceDialect& sequenceDialect();
[[nodiscard]] std::optional<core::ScanSynthRefs> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                          const std::set<u8>& programs,
                                                          std::string_view displayName);
[[nodiscard]] core::FormatDefinition definition();

}  // namespace vgmtrans::formats::falcom_snes
