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

namespace vgmtrans::formats::itikiti_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u32 kCommandLimit = 131072;
inline constexpr u16 kPpqn = 48;
inline constexpr std::string_view kInstrumentDomain = "itikiti-snes.instrument";

struct Layout {
  u16 sequenceHeaderAddress;
  u16 sequenceBaseAddress;
  u16 tuningTableAddress;
  u16 adsrTableAddress;
  u16 spcDirAddress;
  u8 groupIndex;
  u8 trackCount;
  u8 echoDelay;
};

struct ReferencedPrograms {
  std::set<u8> programs;
};

struct SequenceParse {
  core::SequenceProgram program;
  ReferencedPrograms references;
  core::SourceRange headerRange;
};

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, u32 trackNumber, u32 startAddress,
                                                   u32 sequenceBase, u8 groupIndex = 0,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceDialect& sequenceDialect();
[[nodiscard]] core::Envelope driverEnvelope(u8 adsr1, u8 adsr2);
[[nodiscard]] std::optional<core::ScanSynthRefs> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                          const ReferencedPrograms& references,
                                                          std::string_view displayName);
[[nodiscard]] core::FormatDefinition definition();

}  // namespace vgmtrans::formats::itikiti_snes
