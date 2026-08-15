/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceDialect.h"

#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::prism_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kMaxTracks = 24;
inline constexpr u16 kPpqn = 48;
inline constexpr std::string_view kInstrumentDomain = "prism-snes.instrument";

enum class Version : u8 {
  CosmoGang,
  DualOrb,
  Modern,
};

struct TrackHeader {
  u8 logicalChannel = 0;
  u8 physicalChannelFlags = 0;
  u16 startAddress = 0;
  core::SourceRange range;
};

struct Layout {
  Version version = Version::Modern;
  u16 sequenceListAddress = 0;
  u16 sequenceHeaderAddress = 0;
  u16 commandTableAddress = 0;
  u16 spcDirAddress = 0;
  u16 adsr1TableAddress = 0;
  u16 adsr2TableAddress = 0;
  u16 tuningHighTableAddress = 0;
  u16 tuningLowTableAddress = 0;
  u16 alternatePanTableAddress = 0;
  u16 defaultPanTableAddress = 0;
  u8 echoDelay = 0;
  u8 echoFilter = 0;
  u8 songIndex = 0;
  std::vector<TrackHeader> tracks;
};

struct SequenceParse {
  core::SequenceProgram program;
  std::set<u8> programs;
  core::SourceRange headerRange;
};

[[nodiscard]] const char* versionName(Version version);
[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, Version version, u32 trackNumber,
                                                   u32 startAddress, u8 logicalChannel, u8 physicalChannelFlags,
                                                   std::set<u8>* programs = nullptr,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceDialect& sequenceDialect();
[[nodiscard]] core::Envelope driverEnvelope(u8 adsr1, u8 adsr2);
[[nodiscard]] std::optional<core::ScanSynthRefs> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                          const std::set<u8>& programs, std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::prism_snes
