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

namespace vgmtrans::formats::rare_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u16 kPpqn = 32;
inline constexpr std::string_view kInstrumentDomain = "rare-snes.instrument";

// Nine distinct driver binaries are documented. Builds which only relocate
// code share a profile; profiles differ only when their sequence language does.
enum class Profile : u8 {
  Unknown,
  Battlemaniacs,
  BattletoadsDoubleDragon,
  DonkeyKongCountry,
  KillerInstinctBeta,
  WinningRun,
  KillerInstinct,
  DonkeyKongCountry2,
};

struct Layout {
  Profile profile = Profile::Unknown;
  u32 sequenceHeaderAddress = 0;
  core::SourceRange sequenceHeaderRange;
  core::SourceRange initialTempoRange;
  std::vector<u16> trackStarts;
  u8 initialTempo = 0;
  u8 initialTimer = 0;
  bool monoOutput = false;
  std::optional<u32> instrumentTableAddress;
  std::optional<u16> spcDirAddress;
  std::optional<u8> battlemaniacsSong;
};

struct PatchRecipe {
  u32 key = 0;
  u8 sourceProgram = 0;
  u8 srcn = 0;
  s8 tuning = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0x7f;
  core::SourceRange source;

  friend bool operator==(const PatchRecipe&, const PatchRecipe&) = default;
};

struct SequenceRecipes {
  std::vector<PatchRecipe> patches;
};

struct SequenceParse {
  core::SequenceProgram program;
  SequenceRecipes recipes;
};

[[nodiscard]] const char* profileName(Profile profile);
[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, Profile profile, u32 trackNumber,
                                                   u32 startAddress, u32 sequenceDataFloor = 0,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceDialect& sequenceDialect();
[[nodiscard]] core::SequenceRuntime sequenceRuntime(Profile profile, u8 initialTempo, u8 initialTimer,
                                                    bool monoOutput = false);
[[nodiscard]] std::optional<core::ScanSynthRefs> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                          const SequenceRecipes& recipes, std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::rare_snes
