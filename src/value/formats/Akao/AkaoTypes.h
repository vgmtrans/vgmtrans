/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/synth/SynthModel.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::akao {

inline constexpr std::string_view kAkaoFormatName = "Akao";
inline constexpr std::string_view kAkaoCollectionResolver = "Akao";
inline constexpr std::string_view kAkaoSequenceIdDomain = "akao.sequence-id";
inline constexpr std::string_view kAkaoSampleSetDomain = "akao.sample-set";
inline constexpr std::string_view kAkaoArticulationDomain = "akao.articulation";

enum class AkaoPs1Version : u8 {
  Unknown = 0,
  Version1_0,
  Version1_1,
  Version1_2,
  Version2,
  Version3_0,
  Version3_1,
  Version3_2,
};

struct AkaoSequenceHeader {
  u32 offset = 0;
  u32 length = 0;
  AkaoPs1Version version = AkaoPs1Version::Unknown;
  u16 sequenceId = 0;
  std::optional<u16> sampleSetId;
  u32 trackBits = 0;
  u32 trackHeaderOffset = 0;
  std::optional<u32> instrumentSetOffset;
  std::optional<u32> drumSetOffset;
};

struct AkaoSequenceAnalysis {
  AkaoSequenceHeader header;
  std::vector<u32> trackAddresses;
  std::set<u32> customInstrumentOffsets;
  std::set<u32> drumInstrumentOffsets;
  std::set<u32> individualArtIds;
  bool usesIndividualArts = false;
};

struct AkaoArt {
  u32 artId = 0;
  core::SourceRange range;
  u8 unityKey = 60;
  s16 fineTuneCents = 0;
  u32 sampleOffset = 0;
  u32 loopPoint = 0;
  std::optional<core::Loop> loop;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  u32 sampleIndex = 0;
};

struct AkaoSampleCollectionParse {
  core::ScanSampleCollectionRef ref;
  std::optional<u16> sampleSetId;
  u32 offset = 0;
  u32 length = 0;
  AkaoPs1Version version = AkaoPs1Version::Unknown;
  u32 firstArtId = 0;
  u32 artCount = 0;
  std::vector<AkaoArt> arts;
};

struct AkaoArtBinding {
  core::ScanSampleCollectionRef collection;
  u32 sampleIndex = 0;
  AkaoArt art;
};

using AkaoArtMap = std::map<u32, AkaoArtBinding>;

struct AkaoInstrDatLocation {
  u32 instrAllOffset = 0;
  u32 instrDatOffset = 0;
  u32 firstArtId = 0;
  u32 artCount = 0;
};

}  // namespace vgmtrans::formats::akao
