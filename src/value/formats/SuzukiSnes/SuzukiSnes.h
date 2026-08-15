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
#include <string_view>
#include <vector>

namespace vgmtrans::formats::suzuki_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr u32 kDrumKitKey = 0x7f << 7;
inline constexpr u8 kDrumKeyBias = 60;
inline constexpr std::string_view kInstrumentDomain = "suzuki-snes.instrument";

enum class Version : u8 {
  SeikenDensetsu3,
  BahamutLagoon,
  SuperMarioRpg,
};

struct Layout {
  Version version = Version::SeikenDensetsu3;
  u16 sequenceHeaderAddress = 0;
  u16 spcDirAddress = 0;
  u16 srcnTableAddress = 0;
  u16 volumeTableAddress = 0;
  u16 adsrTableAddress = 0;
  u16 tuningTableAddress = 0;
};

// Each five-byte row is copied by the driver from the sequence header into its
// runtime percussion table. Keeping the decoded row here lets synth creation
// consume sequence-owned instrument data without retaining or reparsing bytes.
struct DrumSlot {
  u8 note = 0;
  u8 sourceProgram = 0;
  u8 sourceKey = 0;
  u8 volume = 0;
  u8 pan = 0x80;
  core::SourceRange source;

  friend bool operator==(const DrumSlot&, const DrumSlot&) = default;
};

struct SequenceRecipes {
  std::vector<DrumSlot> drums;
};

struct SequenceParse {
  core::SequenceProgram program;
  SequenceRecipes recipes;
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
[[nodiscard]] core::SequenceRuntime sequenceRuntime(Version version);
[[nodiscard]] std::optional<core::ScanSynthRefs> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                          const SequenceRecipes& recipes, std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::suzuki_snes
