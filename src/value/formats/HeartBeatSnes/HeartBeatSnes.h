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
#include <set>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::heartbeat_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 24;
inline constexpr std::string_view kInstrumentDomain = "heartbeat-snes.instrument";

enum class Version : u8 {
  DragonQuest3,
  DragonQuest6,
};

struct Layout {
  Version version = Version::DragonQuest6;
  u16 sequenceHeaderAddress = 0;
  u16 instrumentTableAddress = 0;
  u16 spcDirAddress = 0;
  u16 srcnTableAddress = 0;
  u8 songIndex = 0;
};

struct SequenceRecipes {
  std::set<u8> programs{0};
};

struct SequenceParse {
  core::SequenceProgram program;
  SequenceRecipes recipes;
  core::SourceRange headerRange;
};

[[nodiscard]] const char* versionName(Version version);
[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, Version version, u32 trackNumber,
                                                   u32 startAddress, u32 sequenceBase,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceDialect& sequenceDialect();
[[nodiscard]] std::optional<core::ScanSynthRefs> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                          const SequenceRecipes& recipes, std::string_view displayName);
[[nodiscard]] core::FormatDefinition definition();

}  // namespace vgmtrans::formats::heartbeat_snes
