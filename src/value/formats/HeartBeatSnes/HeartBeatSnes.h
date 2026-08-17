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
  Version version;
  u16 sequenceHeaderAddress;
  u16 instrumentTableAddress;
  u16 spcDirAddress;
  u16 srcnTableAddress;
  u8 songIndex;
  u8 trackCount;
};

struct SequenceParse {
  core::SequenceProgram program;
  std::set<u8> programs;
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
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] core::SequenceRuntime sequenceRuntime();
[[nodiscard]] core::Envelope driverEnvelope(u8 adsr1, u8 adsr2);
[[nodiscard]] std::optional<core::ScanSoundBankRef> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                             const std::set<u8>& programs,
                                                             std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::heartbeat_snes
