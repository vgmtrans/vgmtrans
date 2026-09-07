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
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::softcreat_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr std::string_view kInstrumentDomain = "softcreat-snes.instrument";

inline constexpr core::Envelope kNeutralGainEnvelope{
    .attackSeconds = 0.0,
    .holdSeconds = 0.0,
    .decaySeconds = std::numeric_limits<double>::infinity(),
    .releaseSeconds = 0.0,
    .sustainAmplitude = 1.0,
};

// Driver generations with materially different command numbering or dispatch.
enum class Version : u8 {
  V1,
  V2,   // shared V2a/V3/V4 command set
  V2b,  // V2 variant with its additional commands
  V5,
  V6,  // shared V6a/V6b command set
  V6c,
  V6d,
  V7,
};

struct Dialect {
  u8 commandCutoff = 0;
  std::optional<u8> noteAliasOpcode;
};

[[nodiscard]] constexpr Dialect dialect(Version version) noexcept {
  constexpr std::array dialects{
      Dialect{0xc4},       // V1
      Dialect{0xb8},       // V2
      Dialect{0xba, 0xb8}, // V2b
      Dialect{0xb9, 0xb9}, // V5; aliases are decoded before the cutoff
      Dialect{0xc7, 0xb9}, // V6
      Dialect{0xbd, 0xb3}, // V6c
      Dialect{0xc3, 0xb9}, // V6d
      Dialect{0xc3, 0xb9}, // V7
  };
  return dialects[static_cast<size_t>(version)];
}

struct TrackPointer {
  u16 address = 0;
  core::SourceRange lowSource;
  core::SourceRange highSource;
};

struct EchoState {
  s8 left = 0;
  s8 right = 0;
  s8 feedback = 0;
  u8 voiceMask = 0;
  u8 delay = 0;
  std::array<s8, 8> fir{};
};

struct Layout {
  Version version = Version::V2;
  u8 songIndex = 0;
  u8 initialTimer = 0x85;
  u16 musicVolume = 0x80;
  u16 pitchLowTableAddress = 0;
  u16 pitchHighTableAddress = 0;
  u16 coarseTableAddress = 0;
  u16 fineTableAddress = 0;
  u16 envelopeTableAddress = 0;
  u16 spcDirAddress = 0;
  std::optional<u16> noteAliasTableAddress;
  core::SourceRange sequenceHeaderRange;
  std::array<TrackPointer, kTrackCount> tracks{};
  EchoState echo;
};

struct SequenceParse {
  core::SequenceProgram program;
  std::set<u8> referencedInstruments{0};
};

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, const Layout& layout, u32 trackNumber,
                                                   u32 startAddress,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] core::SequenceRuntime sequenceRuntime(core::RetainedSource source, const Layout& layout);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const std::set<u8>& referencedInstruments,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::softcreat_snes
