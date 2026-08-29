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

namespace vgmtrans::formats::graph_res_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr u32 kCommandLimit = 131072;
inline constexpr std::string_view kInstrumentDomain = "graph-res-snes.instrument";

struct DspState {
  u8 masterLeft = 0x7f;
  u8 masterRight = 0x7f;
  u8 echoLeft = 0;
  u8 echoRight = 0;
  s8 echoFeedback = 0;
  u8 echoVoices = 0;
  u8 echoDelay = 0;
  u8 flags = 0x20;
  std::array<s8, 8> fir{};
};

struct TrackHeader {
  u8 index = 0;
  u16 startAddress = 0;
  core::SourceRange range;
};

struct Layout {
  u16 sequenceHeaderAddress = 0;
  u16 volumeTableAddress = 0;
  u16 panTableAddress = 0;
  u16 pitchTableAddress = 0;
  u16 pitchEnvelopeListAddress = 0;
  u16 spcDirAddress = 0;
  u8 pitchEnvelopeCount = 0;
  u8 timerTarget = 0;
  DspState dsp;
  std::vector<TrackHeader> tracks;
};

struct SequenceParse {
  core::SequenceProgram program;
  std::set<u8> programs;
  core::SourceRange headerRange;
};

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, const Layout& layout, u32 trackNumber,
                                                   u32 startAddress, std::set<u8>* programs = nullptr,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::RetainedSource source, const Layout& layout,
                                           core::AssetId sequenceId, core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] core::Envelope driverEnvelope(u8 adsr1, u8 adsr2);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const std::set<u8>& programs,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::graph_res_snes
