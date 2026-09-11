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

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::konami_tmnt2 {

inline constexpr std::string_view kFormatName = "KonamiTMNT2";
inline constexpr std::string_view kYm2151Domain = "konami-tmnt2:ym2151";
inline constexpr std::string_view kK053260Domain = "konami-tmnt2:k053260";
inline constexpr u32 kPpqn = 96;
inline constexpr double kChipClock = 3'579'545.0;
inline constexpr double kSampleRate = kChipClock / 112.0;
inline constexpr u32 kInvalidSampleIndex = std::numeric_limits<u32>::max();

enum class Version : u8 {
  Tmnt2,
  SunsetRiders,
  BellsWhistles,
  Vendetta,
};

[[nodiscard]] constexpr u8 k053260PanIndex(u8 raw, Version version) {
  u8 index = raw & 7;
  if (index == 0) {
    index = 4;
  }
  if (version == Version::Vendetta) {
    index = static_cast<u8>(-index) & 7;
  }
  return index;
}

enum class TrackChip : u8 {
  Ym2151,
  K053260,
};

struct SampleInfo {
  u32 start = 0;
  u32 length = 0;
  u32 loopStart = 0;
  u16 pitch = 0;
  bool adpcm = false;
  bool reverse = false;
  bool loops = false;
  core::SourceRange range;

  [[nodiscard]] bool fitsIn(u64 size) const { return start <= size && length <= size - start; }
};

struct SampleInstrument {
  u8 sampleInfo = 0;
  u8 volume = 0x7f;
  u8 gate = 0;
  u8 release = 0;
  u8 pan = 0;
  core::SourceRange range;
  u32 sampleIndex = kInvalidSampleIndex;
};

struct Drum {
  bool valid = false;
  u8 bank = 0;
  u8 slot = 0;
  u8 volume = 0x7f;
  u8 release = 0;
  u8 pan = 0;
  u16 pitch = 0;
  u16 gate = 0;
  core::SourceRange range;
  u32 sampleIndex = 0;
};

struct TrackLayout {
  u32 number = 0;
  TrackChip chip = TrackChip::Ym2151;
  u32 offset = 0;
  core::SourceRange pointer;
};

struct SequenceLayout {
  u32 index = 0;
  core::SourceRange trackTable;
  u32 ymTrackCount = 0;
  u32 totalTrackCount = 0;
  std::vector<TrackLayout> tracks;
  std::string name;

  [[nodiscard]] std::string trackName(u32 track) const {
    const bool fm = track < ymTrackCount;
    return (fm ? "FM Track " : "Sampled Track ") + std::to_string(fm ? track : track - ymTrackCount);
  }
};

struct SequencePointerLayout {
  u32 slot = 0;
  u16 encoded = 0;
  u32 sequenceIndex = 0;
  core::SourceRange range;
};

struct Layout {
  Version version = Version::Tmnt2;
  std::string game;
  core::SourceRange program;
  core::SourceRange sound;
  u32 sequenceTableOffset = 0;
  u32 ym2151TableOffset = 0;
  u32 k053260TableOffset = 0;
  u32 drumTableOffset = 0;
  u8 clkb = 0xf2;
  u8 defaultTickSkipInterval = 0;
  core::SourceRange sequenceTable;
  std::vector<SequencePointerLayout> sequencePointers;
  std::vector<SequenceLayout> sequences;
  std::vector<u32> ym2151Patches;
  std::vector<SampleInfo> sampleInfos;
  std::vector<SampleInstrument> sampleInstruments;
  std::vector<std::vector<Drum>> drumBanks;
};

[[nodiscard]] double driverTickRate(u8 clkb, u8 skipInterval);
[[nodiscard]] double sampledReleaseSeconds(Version version, u8 packed, u8 volume, double ticksPerSecond);
[[nodiscard]] std::optional<Layout> findLayout(const core::SourceFile& source, core::ByteReader reader,
                                               std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] core::SequenceProgram decodeSequence(core::ByteReader reader, const Layout& layout,
                                                   const SequenceLayout& sequence, core::AssetId sequenceAsset,
                                                   core::SourceMapBuilder* sourceMap = nullptr,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] std::vector<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::konami_tmnt2
