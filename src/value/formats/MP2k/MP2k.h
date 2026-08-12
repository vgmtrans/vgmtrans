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

#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::mp2k {

inline constexpr std::string_view kMp2kFormatName = "MP2k";
inline constexpr std::string_view kMp2kSequenceDialectId = "mp2k:mplay";

struct Mp2kEngine {
  u32 settingsOffset = 0;
  u32 songTableOffset = 0;
  u32 sampleRate = 0;
  u8 directSoundMasterVolume = 15;
  u8 dacBits = 8;
  u8 reverb = 0;
};

struct Mp2kSong {
  u32 index = 0;
  u32 offset = 0;
  u32 bankOffset = 0;
  u8 declaredTracks = 0;
  u8 activeTracks = 0;
  u8 reverb = 0;
};

struct Mp2kBank {
  u32 offset = 0;
  u32 instrumentCount = 0;
};

struct Mp2kLayout {
  Mp2kEngine engine;
  std::vector<Mp2kSong> songs;
  std::vector<Mp2kBank> banks;
};

[[nodiscard]] std::vector<Mp2kLayout> findMp2kLayouts(core::ScanResultBuilder& builder);

[[nodiscard]] const core::SequenceDialect& mp2kSequenceDialect();
[[nodiscard]] core::SequenceProgram parseMp2kSequenceProgram(core::ByteReader reader, core::AssetId id,
                                                             const Mp2kSong& song,
                                                             core::SourceMapBuilder* sourceMap = nullptr,
                                                             std::vector<core::Diagnostic>* diagnostics = nullptr);

[[nodiscard]] core::ScanSampleCollectionDraft addMp2kPsgSamples(core::ScanResultBuilder& builder, u32 sampleRate);
[[nodiscard]] core::ScanInstrumentSetDraft addMp2kInstrumentSet(
    core::ScanResultBuilder& builder, const Mp2kBank& bank, u32 sampleRate, u8 directSoundMasterVolume, u8 dacBits,
    core::ScanSampleCollectionDraft& psgSamples, std::optional<core::ScanSampleCollectionDraft>& pcmSamples);

[[nodiscard]] core::FormatDefinition mp2kDefinition();

}  // namespace vgmtrans::formats::mp2k
