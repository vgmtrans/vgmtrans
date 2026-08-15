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

namespace vgmtrans::formats::compile_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u16 kPpqn = 12;
inline constexpr u32 kCommandLimit = 131072;
inline constexpr std::string_view kInstrumentDomain = "compile-snes.instrument";

enum class Version : u8 {
  Aleste,
  JakiCrush,
  SuperPuyo,
  Standard,
};

struct Layout {
  Version version;
  u16 engineHeaderAddress;
  u16 songListAddress;
  u16 songHeaderAddress;
  u8 songIndex;
  u16 durationTableAddress;
  u16 percussionTableAddress;
  u16 volumeEnvelopeTableAddress;
  u16 vibratoTableAddress;
  u16 gainEnvelopeTableAddress;
  u16 adsrTableAddress;
  u16 echoPresetTableAddress;
  u16 tuningTableAddress;
  u16 panEnvelopeTableAddress;
  u16 pitchTableListAddress;
  u16 regularPitchTableAddress;
  u16 spcDirAddress;
  s8 globalTranspose;
  bool stereoEnabled;

  [[nodiscard]] bool early() const noexcept { return version == Version::Aleste || version == Version::JakiCrush; }
  [[nodiscard]] bool hasEchoCommands() const noexcept {
    return version == Version::Aleste || version == Version::SuperPuyo;
  }
};

struct ReferencedData {
  std::set<u8> programs;
  std::set<u8> volumeEnvelopes;
  std::set<u8> vibratos;
  std::set<u8> gainEnvelopes;
  std::set<u8> panEnvelopes;
  std::set<u8> echoPresets;
  std::set<u8> percussionNotes;
};

struct SequenceParse {
  core::SequenceProgram program;
  ReferencedData references;
  core::SourceRange headerRange;
};

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, const Layout& layout, u32 trackNumber,
                                                   u32 startAddress,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceDialect& sequenceDialect();
[[nodiscard]] core::Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain = 0);
[[nodiscard]] std::optional<core::ScanSynthRefs> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                          const ReferencedData& references,
                                                          std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::compile_snes
