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
#include <set>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::ascii_shuichi_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr std::string_view kInstrumentDomain = "ascii-shuichi-snes.instrument";

enum class Version : u8 {
  Early,
  Later,
};

struct Layout {
  Version version;
  u8 noteBase;
  u16 sequenceHeaderAddress;
  u16 instrumentTableAddress;
  u16 tuningTableAddress;
  u16 spcDirAddress;
  u16 commandTableAddress;
  bool hasEchoFirCommand;
  std::array<u16, kTrackCount> trackAddresses;
};

struct Patch {
  u8 program;
  u8 srcn;
  u8 adsr1;
  u8 adsr2;
  u8 gain;
  s8 tuning;
  core::SourceRange source;
  core::SourceRange tuningSource;
};

struct SequenceParse {
  core::SequenceProgram program;
  std::set<u8> programs;
  core::SourceRange headerRange;
};

[[nodiscard]] const char* versionName(Version version);
[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, const Layout& layout, u32 trackNumber,
                                                   u32 startAddress,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] core::Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const std::set<u8>& programs,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::ascii_shuichi_snes
