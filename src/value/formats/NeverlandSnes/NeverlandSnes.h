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

namespace vgmtrans::formats::neverland_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u32 kCommandLimit = 131072;
inline constexpr u16 kPpqn = 48;
inline constexpr std::string_view kInstrumentDomain = "neverland-snes.instrument";

enum class Version : u8 {
  Original,
  Modern,
};

struct TrackLayout {
  bool active = false;
  bool percussion = false;
  u16 playlistAddress = 0;
  core::SourceRange pointerRange;
};

struct PercussionPatch {
  u8 key = 0;
  u16 pitch = 0;
  u8 pan = 0x40;
  u8 program = 0;
};

struct Layout {
  Version version = Version::Modern;
  u16 sequenceBaseAddress = 0;
  u16 instrumentTableAddress = 0;
  u16 spcDirAddress = 0;
  u8 initialTempo = 0;
  u8 initialMasterVolume = 0x70;
  u8 initialEchoDelay = 0;
  u8 initialEchoVolume = 0x54;
  u8 initialEchoFeedback = 0x50;
  u8 initialEchoFilter = 0;
  bool hasPitchDrift = false;
  std::array<TrackLayout, kTrackCount> tracks;
  std::vector<PercussionPatch> percussion;
};

struct ReferencedPrograms {
  std::set<u8> programs;
};

// Instrument bytes +2/+3 are an unsigned 8.8 multiplier in source order.
[[nodiscard]] constexpr double instrumentPitchScale(u16 tuning) {
  return static_cast<double>(tuning == 0 ? 1 : tuning) / 256.0;
}

struct SequenceParse {
  core::SequenceProgram program;
  ReferencedPrograms references;
  core::SourceRange headerRange;
};

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, const Layout& layout, u32 trackNumber,
                                                   u32 playlistAddress,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig(Version version);
[[nodiscard]] core::SequenceRuntime sequenceRuntime(core::ByteReader reader, const Layout& layout);
[[nodiscard]] core::Envelope driverEnvelope(u8 adsr1, u8 adsr2);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const ReferencedPrograms& references,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::neverland_snes
