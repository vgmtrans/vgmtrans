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

namespace vgmtrans::formats::softcreat {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr std::string_view kInstrumentDomain = "softcreat.instrument";

// The command cutoff identifies five materially different sequence languages.
enum class Version : u8 {
  Early,            // Equinox; Spider-Man and the X-Men
  Plok,
  MaximumCarnage,
  LateEcho,         // The Tick; Ken Griffey Jr. MLB
  LateNoEcho,       // Tin Star; Foreman For Real
};

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
  Version version = Version::Early;
  u8 commandCutoff = 0;
  u8 songIndex = 0;
  u8 songCount = 0;
  u8 initialTimer = 0x85;
  u8 musicVolume = 0x80;
  u16 songListAddress = 0;
  u16 sequenceHeaderAddress = 0;
  u16 dispatchTableAddress = 0;
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

struct SequenceReferences {
  std::set<u8> srcns{0};
};

struct SequenceParse {
  core::SequenceProgram program;
  SequenceReferences references;
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
[[nodiscard]] core::SequenceRuntime sequenceRuntime(core::RetainedSource source, const Layout& layout);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const SequenceReferences& references,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::softcreat
