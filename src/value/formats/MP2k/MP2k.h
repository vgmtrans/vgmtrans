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
#include <span>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::mp2k {

inline constexpr std::string_view kMp2kFormatName = "MP2k";

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

struct Mp2kTone {
  u8 type = 0;
  u8 key = 60;
  u8 length = 0;
  u8 panSweep = 0;
  u32 wave = 0;
  u8 attack = 0;
  u8 decay = 0;
  u8 sustain = 0;
  u8 release = 0;
  core::SourceRecord source;

  [[nodiscard]] u8 cgbType() const { return type & 0x07; }
  [[nodiscard]] bool fixed() const { return (type & 0x08) != 0; }
  [[nodiscard]] bool reverse() const { return (type & 0x10) != 0; }
  [[nodiscard]] bool split() const { return (type & 0x40) != 0; }
  [[nodiscard]] bool rhythm() const { return (type & 0x80) != 0; }
  [[nodiscard]] bool table() const { return split() || rhythm(); }
};

struct Mp2kScannedBank {
  core::ScanSoundBankDraft instruments;
  std::vector<Mp2kTone> tones;
};

struct Mp2kLayout {
  Mp2kEngine engine;
  std::vector<Mp2kSong> songs;
  std::vector<Mp2kBank> banks;
};

[[nodiscard]] std::vector<Mp2kLayout> findMp2kLayouts(core::ScanResultBuilder& builder);

[[nodiscard]] const core::SequenceProgramConfig& mp2kSequenceConfig();
[[nodiscard]] core::SequenceProgram parseMp2kSequenceProgram(core::ByteReader reader, core::AssetId id,
                                                             const Mp2kSong& song, std::span<const Mp2kTone> tones,
                                                             core::SourceMapBuilder* sourceMap = nullptr,
                                                             std::vector<core::Diagnostic>* diagnostics = nullptr);

[[nodiscard]] core::ScanSamplePoolDraft addMp2kPsgSamples(core::ScanResultBuilder& builder, u32 sampleRate);
[[nodiscard]] Mp2kScannedBank addMp2kInstrumentSet(core::ScanResultBuilder& builder, const Mp2kBank& bank,
                                                   u32 sampleRate, u8 directSoundMasterVolume, u8 dacBits,
                                                   core::ScanSamplePoolDraft& psgSamples);

[[nodiscard]] core::FormatModule mp2kModule();

}  // namespace vgmtrans::formats::mp2k
