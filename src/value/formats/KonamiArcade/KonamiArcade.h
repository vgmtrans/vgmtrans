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

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::konami_arcade {

inline constexpr std::string_view kKonamiArcadeFormatName = "KonamiArcade";
inline constexpr std::string_view kKonamiArcadeSequenceDialectId = "konami-arcade:k054539";
inline constexpr std::string_view kKonamiArcadeInstrumentDomain = "konami-arcade:instrument";
inline constexpr u32 kKonamiArcadePpqn = 48;
inline constexpr u32 kKonamiArcadeSampleRate = 24000;
inline constexpr u32 kKonamiArcadeMaxTracks = 16;

enum class KonamiArcadeVersion : u8 {
  MysticWarrior = 1,
  Gx = 2,
};

enum class KonamiSampleType : u8 {
  Pcm8 = 0,
  Pcm16 = 4,
  Adpcm = 8,
  Unknown = 0xff,
};

struct KonamiArcadeSampleInfo {
  u32 loopOffset = 0;
  u32 startOffset = 0;
  KonamiSampleType type = KonamiSampleType::Pcm8;
  bool reverse = false;
  bool loops = false;
  u8 attenuation = 0;
  core::SourceRange range;
};

struct KonamiArcadeDrum {
  u8 sample = 0;
  u8 unityKey = 0;
  // GX interprets this as the fractional byte of an unsigned 8.8 pitch.
  // MysticWarrior interprets bits 2..7 as unsigned six-bit sixteenths.
  u8 pitch = 0;
  u8 pan = 0;
  u8 defaultDuration = 0;
  u8 attenuation = 0;
  core::SourceRange range;
};

[[nodiscard]] double konamiArcadeDrumPitch(KonamiArcadeVersion version, const KonamiArcadeDrum& drum);

struct KonamiArcadeTrackLayout {
  u32 number = 0;
  u64 encodedAddress = 0;
  u32 offset = 0;
  core::SourceRange pointer;
};

struct KonamiArcadeSequenceLayout {
  u32 index = 0;
  u32 offset = 0;
  u32 memoryBase = 0;
  u32 indexedNoteTableOffset = 0;
  s8 initialAttenuation = 0;
  s8 initialTranspose = 0;
  s8 tempoOffset = 0;
  core::SourceRange tableEntry;
  core::SourceRange trackTable;
  std::vector<KonamiArcadeTrackLayout> tracks;
  std::string name;
};

struct KonamiArcadeLayout {
  KonamiArcadeVersion version = KonamiArcadeVersion::MysticWarrior;
  std::string game;
  core::SourceRange code;
  core::SourceRange sound;
  u32 sequenceTableOffset = 0;
  u32 sampleTablesOffset = 0;
  std::optional<u32> drumSampleTableOffset;
  std::optional<u32> drumTableOffset;
  double nmiRateHertz = 0.0;
  std::vector<KonamiArcadeSequenceLayout> sequences;
  std::vector<KonamiArcadeSampleInfo> sampleInfos;
  u32 melodicSampleCount = 0;
  std::array<KonamiArcadeDrum, 46> drums;
  u32 drumCount = 0;
};

[[nodiscard]] const char* konamiArcadeVersionName(KonamiArcadeVersion version);
[[nodiscard]] std::optional<KonamiArcadeLayout> findKonamiArcadeLayout(
    const core::SourceFile& source, core::ByteReader reader, std::vector<core::Diagnostic>* diagnostics = nullptr);

[[nodiscard]] const core::SequenceDialect& konamiArcadeSequenceDialect();
[[nodiscard]] core::SequenceProgram decodeKonamiArcadeSequence(core::ByteReader reader,
                                                               const KonamiArcadeLayout& layout,
                                                               const KonamiArcadeSequenceLayout& sequence,
                                                               core::AssetId sequenceAsset,
                                                               core::SourceMapBuilder* sourceMap = nullptr,
                                                               std::vector<core::Diagnostic>* diagnostics = nullptr);

[[nodiscard]] core::ScanSynthRefs addKonamiArcadeSynth(core::ScanResultBuilder& builder,
                                                       const KonamiArcadeLayout& layout);

[[nodiscard]] core::FormatDefinition konamiArcadeDefinition();

}  // namespace vgmtrans::formats::konami_arcade
