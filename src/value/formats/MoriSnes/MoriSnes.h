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

#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::mori_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 10;
inline constexpr u32 kCommandLimit = 131072;
// Source waits are decremented by an 8-bit tempo accumulator, while hardware
// voice gates use every Timer 0 interrupt. Keeping the accumulator denominator
// in the timeline represents both clocks exactly.
inline constexpr u16 kPpqn = 48 * 256;
inline constexpr std::string_view kInstrumentDomain = "mori-snes.instrument";
inline constexpr u32 kDirectInstrumentFlag = 0x10000;

struct TrackHeader {
  u8 channel = 0;
  u16 startAddress = 0;
  core::SourceRange range;
};

struct SfxVoice {
  u16 scriptAddress = 0;
  core::SourceRange range;
};

struct Layout {
  u16 songListAddress = 0;
  u16 songHeaderAddress = 0;
  u16 spcDirAddress = 0;
  u16 presetTableAddress = 0;
  u16 panTableAddress = 0;
  u8 songIndex = 0;
  std::vector<TrackHeader> tracks;
  std::vector<SfxVoice> sfxVoices;
};

struct ReferencedInstruments {
  std::set<u16> descriptors;
  std::map<u16, std::set<u8>> noteKeys;
  // Percussion descriptors select a script by the raw five-bit note. The
  // sounding MIDI key can differ after D1/C4, so retain both facts.
  std::map<u16, std::map<u8, std::set<u8>>> percussionKeys;
  std::map<u16, std::set<u8>> directScriptKeys;
};

struct SequenceParse {
  core::SequenceProgram program;
  ReferencedInstruments references;
  core::SourceRange headerRange;
};

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, const Layout& layout, u32 trackNumber,
                                                   u32 startAddress,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] core::SequenceRuntime sequenceRuntime(core::ByteReader reader, const Layout& layout);
[[nodiscard]] core::Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const ReferencedInstruments& references,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::mori_snes
