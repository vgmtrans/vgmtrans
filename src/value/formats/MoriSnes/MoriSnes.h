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

enum class Version : u8 {
  Gokinjo,
  Shien,
};

struct DriverTraits {
  Version version;
  u8 timerTarget;
  u8 initialPan;
  u8 maximumPan;
  u8 initialBendRange;
  bool absolutePercussionPointers;

  [[nodiscard]] constexpr double timerMilliseconds() const { return timerTarget * 0.125; }
  [[nodiscard]] constexpr double timerSeconds() const { return timerTarget * 0.000125; }
};

[[nodiscard]] constexpr DriverTraits driverTraits(Version version) {
  return version == Version::Shien
             ? DriverTraits{.version = version,
                            .timerTarget = 0x3c,
                            .initialPan = 0x0a,
                            .maximumPan = 0x14,
                            .initialBendRange = 0x28,
                            .absolutePercussionPointers = true}
             : DriverTraits{.version = version,
                            .timerTarget = 0x4f,
                            .initialPan = 0x10,
                            .maximumPan = 0x20,
                            .initialBendRange = 0x20,
                            .absolutePercussionPointers = false};
}

// Raw Shien status -> equivalent Gokinjo status. Zeroes are real no-ops in the
// SPC dispatch table; $F7 alone consumes a synchronization-counter operand.
inline constexpr std::array<u8, 56> kShienCommands{
    0xc0, 0xc1, 0,    0,    0,    0xc2, 0,    0xc3, 0,    0,    0xc4, 0,    0,    0xc5, 0,    0xc6,  // Cx
    0,    0,    0,    0,    0xc7, 0xc8, 0xc9, 0xca, 0,    0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0,    0,     // Dx
    0xd0, 0xd1, 0xd2, 0xd3, 0,    0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,  // Ex
    0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0,                                                  // Fx
};

[[nodiscard]] constexpr std::optional<u8> canonicalCommand(Version version, u8 status) {
  if (version == Version::Gokinjo) {
    return status >= 0xc0 && status <= 0xe6 ? std::optional{status} : std::nullopt;
  }
  const u8 command = status >= 0xc0 && status <= 0xf7 ? kShienCommands[status - 0xc0] : 0;
  return command != 0 ? std::optional{command} : std::nullopt;
}

[[nodiscard]] constexpr bool isCommand(Version version, u8 status) {
  return status >= 0xc0 && status <= (version == Version::Shien ? 0xf7 : 0xe6);
}

[[nodiscard]] constexpr u8 commandSize(Version version, u8 status) {
  constexpr std::array<u8, 39> sizes{
      2, 1, 1, 1, 1, 1, 1, 1, 0, 0, 4, 2, 2, 0, 1, 0, 0, 1, 0, 0,
      0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1,
  };
  if (version == Version::Shien && status == 0xf7) {
    return 1;
  }
  const auto canonical = canonicalCommand(version, status);
  return canonical ? sizes[*canonical - 0xc0] : 0;
}

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
  Version version = Version::Gokinjo;
  u16 songListAddress = 0;
  u16 songHeaderAddress = 0;
  u16 spcDirAddress = 0;
  u16 presetTableAddress = 0;
  u16 presetPitchHighAddress = 0;
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
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig(Version version = Version::Gokinjo);
[[nodiscard]] core::SequenceRuntime sequenceRuntime(core::ByteReader reader, const Layout& layout);
[[nodiscard]] core::Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const ReferencedInstruments& references,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::mori_snes
