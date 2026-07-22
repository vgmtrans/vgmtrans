/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"
#include "value/scan/FormatModule.h"
#include "value/scan/FormatDefinition.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/SequenceDialect.h"
#include "value/synth/SynthModel.h"

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::akao {

inline constexpr std::string_view kAkaoFormatName = "Akao";
inline constexpr std::string_view kAkaoCollectionResolver = "Akao";
inline constexpr std::string_view kAkaoSequenceIdDomain = "akao.sequence-id";
inline constexpr std::string_view kAkaoSampleSetDomain = "akao.sample-set";
inline constexpr std::string_view kAkaoArticulationDomain = "akao.articulation";

inline constexpr u32 kAkaoSignature = 0x414B414F;
inline constexpr u32 kAkaoPpqn = 0x30;
inline constexpr u32 kAkaoMaxTrackCommands = 262144;

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

struct AkaoProfile {
  AkaoPs1Version version = AkaoPs1Version::Unknown;

  [[nodiscard]] bool legacyFamily() const noexcept;
  [[nodiscard]] bool version3OrLater() const noexcept;
  [[nodiscard]] bool version32() const noexcept { return version == AkaoPs1Version::Version3_2; }

  [[nodiscard]] bool isSubEventPrefix(u8 status) const noexcept;
  [[nodiscard]] bool isNoteOpcode(u8 status) const noexcept;
  [[nodiscard]] bool noteHasInlineDuration(u8 status) const noexcept;
  [[nodiscard]] bool hasLegacySampleHeader() const noexcept;
  [[nodiscard]] bool hasCompactArticulations() const noexcept;

  [[nodiscard]] u32 directOperandBytes(u8 status) const noexcept;
  [[nodiscard]] u32 subOperandBytes(u8 sub) const noexcept;
  [[nodiscard]] u32 articulationSize() const noexcept;
  [[nodiscard]] u32 legacySampleEndingArticulationId(core::ByteReader reader, u32 offset) const;
  [[nodiscard]] u32 spuDestinationAddress(core::ByteReader reader, u32 sampleCollectionOffset) const;
  [[nodiscard]] u32 legacyDrumRegionBytes() const noexcept;
  [[nodiscard]] bool legacyDrumRegionIsBlank(core::ByteReader reader, u32 regionOffset) const;
  [[nodiscard]] u32 relativeDestination(u32 operandOffset, s16 relative) const noexcept;
  [[nodiscard]] u32 trackAllocationBitsOffset() const noexcept;
  [[nodiscard]] u32 trackHeaderOffset() const noexcept;
  [[nodiscard]] u32 sequenceLength(core::ByteReader reader, u32 offset) const;
  [[nodiscard]] double tempoBpm(u16 tempo) const;
  [[nodiscard]] u32 tempoMicrosPerQuarter(u16 tempo) const;
};

[[nodiscard]] std::string versionName(AkaoPs1Version version);
[[nodiscard]] std::string dialectId(AkaoPs1Version version);
[[nodiscard]] AkaoPs1Version determineVersionFromSource(const core::SourceFile& source);
[[nodiscard]] AkaoPs1Version guessSequenceVersion(core::ByteReader reader, u32 offset);
[[nodiscard]] AkaoPs1Version guessSampleVersion(core::ByteReader reader, u32 offset);

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

// These references are discovered in sequence commands but are needed later
// when collection matching has selected the available sample sets.
struct AkaoSequenceReferences {
  std::set<u32> customInstrumentTableOffsets;
  std::set<u32> drumInstrumentTableOffsets;
  std::set<u32> individualArticulationIds;
  bool usesIndividualArticulations = false;

  void merge(const AkaoSequenceReferences& other);
};

struct AkaoSequenceAnalysis {
  AkaoSequenceHeader header;
  AkaoSequenceReferences references;
  std::vector<u32> requiredArticulations;
};

struct AkaoSequenceParse {
  core::SequenceProgramAsset asset;
  AkaoSequenceAnalysis analysis;
};

// An articulation is Akao's complete description of one playable sample:
// its location, tuning, loop, and envelope all travel together.
struct AkaoArticulation {
  u32 articulationId = 0;
  core::SourceRecord source;
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
  u32 firstArticulationId = 0;
  u32 articulationCount = 0;
  std::vector<AkaoArticulation> articulations;
};

struct AkaoArticulationBinding {
  core::ScanSampleCollectionRef collection;
  u32 sampleIndex = 0;
  AkaoArticulation articulation;
};

using AkaoArticulationMap = std::map<u32, AkaoArticulationBinding>;

// A few early games keep the sample header and articulation table in separate
// fixed locations instead of placing them together in an AKAO block.
struct AkaoSplitSampleLocation {
  u32 sampleHeaderOffset = 0;
  u32 articulationTableOffset = 0;
  u32 firstArticulationId = 0;
  u32 articulationCount = 0;
};

[[nodiscard]] core::SequenceDialect makeAkaoDialect(AkaoPs1Version version);
[[nodiscard]] core::TrackProgram decodeAkaoTrack(AkaoPs1Version version, const core::TrackDecodeScope& tracks,
                                                 u32 trackIndex, u32 startOffset,
                                                 std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] AkaoSequenceReferences akaoSequenceReferences(const core::TrackProgram& track);
[[nodiscard]] std::optional<AkaoSequenceAnalysis> analyzeAkaoSequence(const core::ScanInput& input,
                                                                      const core::SequenceProgramAsset& sequence);
[[nodiscard]] std::optional<AkaoSequenceParse> parseAkaoSequence(const core::ScanInput& input, core::AssetId id,
                                                                 u32 offset,
                                                                 core::SourceMapBuilder* sourceMap = nullptr,
                                                                 std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] std::vector<core::SequenceDialect> akaoSequenceDialects();

[[nodiscard]] std::optional<AkaoSplitSampleLocation> ff7HardcodedAkaoSampleLocation(core::ByteReader reader);
[[nodiscard]] bool isPossibleAkaoSampleCollection(core::ByteReader reader, u32 offset);
[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollectionData(const core::ScanInput& input,
                                                                                     core::ScanSampleCollectionRef ref,
                                                                                     u32 offset,
                                                                                     AkaoPs1Version version);
[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollectionData(const core::ScanInput& input,
                                                                                     core::ScanSampleCollectionRef ref,
                                                                                     AkaoSplitSampleLocation location);
[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollection(const core::ScanInput& input,
                                                                                 core::ScanResultBuilder& result,
                                                                                 core::ScanSampleCollectionRef ref,
                                                                                 u32 offset, AkaoPs1Version version);
[[nodiscard]] std::optional<AkaoSampleCollectionParse> parseAkaoSampleCollection(const core::ScanInput& input,
                                                                                 core::ScanResultBuilder& result,
                                                                                 core::ScanSampleCollectionRef ref,
                                                                                 AkaoSplitSampleLocation location);

[[nodiscard]] std::string akaoInstrumentSetName(const AkaoSequenceAnalysis& sequence);
void buildAkaoInstrumentSet(const core::ScanInput& input, const AkaoSequenceAnalysis& sequence,
                            const AkaoArticulationMap& articulations, core::InstrumentSetBuilder& instruments);
[[nodiscard]] std::vector<u32> analyzeAkaoInstrumentStructures(
    core::ByteReader reader, const AkaoSequenceAnalysis& sequence, core::SourceMapBuilder* sourceMap = nullptr,
    std::optional<core::SourceAnnotationId> parent = std::nullopt);

[[nodiscard]] std::vector<core::DesiredCollection> resolveAkaoCollections(const core::MatchContext& context);
[[nodiscard]] core::PreparedCollectionAssets prepareAkaoCollection(const core::CollectionPrepareContext& context);

[[nodiscard]] core::FormatDefinition akaoDefinition();

}  // namespace vgmtrans::formats::akao
