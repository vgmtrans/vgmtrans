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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::wolf_team_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u16 kPpqn = 48;
inline constexpr u8 kInstrumentCount = 0x40;
inline constexpr std::string_view kInstrumentDomain = "wolf-team-snes.instrument";

enum class Variant : u8 {
  Arcus,
  Middle,
  DarkKingdom,
  AceONerae,
  LeadingJockey,
  TenshiNoUta,
  StarOcean,
  ParlorMini,
  TalesOfPhantasia,
  LateFamily,
};

[[nodiscard]] constexpr bool isMiddleSegmentedVariant(Variant variant) noexcept {
  return variant == Variant::Middle || variant == Variant::DarkKingdom || variant == Variant::AceONerae;
}

[[nodiscard]] constexpr bool isSegmentedVariant(Variant variant) noexcept {
  return variant == Variant::Arcus || isMiddleSegmentedVariant(variant);
}

struct LateTraits {
  u8 specialInstrumentUpper = 0x48;
  bool remapHighInstrumentIds = true;
  bool hasInstrument5KeySplit = false;
  bool programChangeHasDelay = false;

  friend bool operator==(const LateTraits&, const LateTraits&) = default;
};

struct ChannelLayout {
  u8 index = 0;
  u8 status = 0;
  u16 pointerTableAddress = 0;
  core::SourceRange descriptorRange;
  std::vector<u16> streamStarts;
};

struct InstrumentLayout {
  u16 sampleDirAddress = 0;
  u16 patchTableAddress = 0;
  std::optional<u16> patchMapAddress;
  std::optional<u16> volumeTableAddress;
  u8 globalPitchBase = 0;
  u8 entrySize = 0;
  u8 count = kInstrumentCount;
  bool confirmed = false;
};

struct Layout {
  Variant variant = Variant::LateFamily;
  u16 sequenceHeaderAddress = 0;
  u8 relocationPage = 0;
  u32 headerLength = 0;
  u32 middleCommandTableAddress = 0;
  LateTraits lateTraits;
  std::vector<ChannelLayout> channels;
  InstrumentLayout instruments;

  [[nodiscard]] bool segmented() const noexcept { return isSegmentedVariant(variant); }
  [[nodiscard]] bool middleSegmented() const noexcept { return isMiddleSegmentedVariant(variant); }
};

struct SequenceParse {
  core::SequenceProgram program;
  core::SourceRange headerRange;
};

[[nodiscard]] const char* variantName(Variant variant);
[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, const Layout& layout,
                                                   const ChannelLayout& channel,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] std::optional<core::ScanSoundBankRef> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                             std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::wolf_team_snes
