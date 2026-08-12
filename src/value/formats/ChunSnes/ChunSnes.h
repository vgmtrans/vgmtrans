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
#include <string_view>

namespace vgmtrans::formats::chun_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr std::string_view kInstrumentDomain = "chun-snes.instrument";

enum class Version : u8 {
  Summer,
  Winter,
  WinterV3,
};

struct EchoState {
  s8 left = 0;
  s8 right = 0;
  s8 feedback = 0;
  u8 delay = 0;
};

struct Layout {
  Version version = Version::Summer;
  u16 songListAddress = 0;
  u16 sequenceHeaderAddress = 0;
  u16 instrumentSetAddress = 0;
  u16 srcnTableAddress = 0;
  u16 sampleInfoTableAddress = 0;
  u16 spcDirAddress = 0;
  u16 pitchEnvelopeTableAddress = 0;
  u16 miniSequenceTableAddress = 0;
  u16 durationScriptTableAddress = 0;
  u16 gainEnvelopeTableAddress = 0;
  u16 pitchReference = 0x1ede;
  u8 songIndex = 0;
  EchoState echo;
};

struct SequenceParse {
  core::SequenceProgram program;
  core::SourceRange headerRange;
};

[[nodiscard]] const char* versionName(Version version);
[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, Version version, u32 trackNumber,
                                                   u32 startAddress,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceDialect& sequenceDialect();
[[nodiscard]] std::optional<core::ScanSynthRefs> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                          std::string_view displayName);
[[nodiscard]] core::FormatDefinition definition();

}  // namespace vgmtrans::formats::chun_snes
