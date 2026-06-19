/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoModule.h"

#include "value/formats/Akao/AkaoSequence.h"
#include "value/formats/Akao/AkaoSynth.h"
#include "value/scan/CollectionResolver.h"
#include "value/scan/FormatRegistry.h"
#include "value/scan/ScanResultBuilder.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

constexpr u32 kAkaoSignature = 0x414B414F;

struct SequenceFactEntry {
  AssetId asset;
  std::string name;
  std::optional<SourceId> sourceId;
  const SourceFile* source = nullptr;
  u32 sequenceId = 0;
  std::optional<u32> sampleSetId;
  u32 offset = 0;
};

struct InstrumentFactEntry {
  AssetId asset;
  std::optional<SourceId> sourceId;
  u32 sequenceId = 0;
};

struct SampleFactEntry {
  AssetId asset;
  std::optional<SourceId> sourceId;
  const SourceFile* source = nullptr;
  std::optional<u32> sampleSetId;
  u32 firstArt = 0;
  u32 artCount = 0;
  u32 scanOrdinal = 0;
};

[[nodiscard]] bool extensionIsPsf(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return ext == ".psf" || ext == ".minipsf" || ext == ".psflib";
}

[[nodiscard]] bool psfLike(const SourceFile* source) {
  return source != nullptr && extensionIsPsf(source->path);
}

[[nodiscard]] FormatSpecificFact akaoFact(std::string kind, std::vector<MatchField> fields) {
  return FormatSpecificFact{
      .kind = std::move(kind),
      .fields = std::move(fields),
  };
}

void addOptionalField(std::vector<MatchField>& fields, std::string name, std::optional<u32> value) {
  if (value) {
    fields.push_back(MatchField{.name = std::move(name), .value = std::to_string(*value)});
  }
}

[[nodiscard]] bool covers(const SampleFactEntry& sample, u32 artId) {
  return artId >= sample.firstArt && artId < sample.firstArt + sample.artCount;
}

[[nodiscard]] bool sameSource(const std::optional<SourceId>& a, const std::optional<SourceId>& b) {
  return a && b && *a == *b;
}

[[nodiscard]] bool sameSampleSet(std::optional<u32> sequenceSampleSet, std::optional<u32> sampleSet) {
  // Legacy Akao treats missing and zero sample-set ids as the same anonymous set.
  // Several PSF rips omit the ids entirely but still expect the source-local sample
  // collection to attach when instruments do not expose useful articulation refs.
  return sequenceSampleSet.value_or(0) == sampleSet.value_or(0);
}

[[nodiscard]] CollectionKey collectionKey(const SequenceFactEntry& sequence) {
  return CollectionKey{
      .resolver = std::string(kAkaoCollectionResolver),
      .value = "seq:" + std::to_string(sequence.sequenceId) + ":source:" +
               std::to_string(sequence.sourceId ? sequence.sourceId->value : 0) + ":offset:" +
               std::to_string(sequence.offset),
  };
}

[[nodiscard]] std::string missingSampleMessage(const SequenceFactEntry& sequence) {
  if (sequence.sampleSetId) {
    return "Akao sequence references sample set " + std::to_string(*sequence.sampleSetId) +
           ", but no matching sample collection was found";
  }
  return "Akao sequence has no matching sample collection";
}

[[nodiscard]] std::vector<SequenceFactEntry> sequenceFacts(const MatchFactIndex& index) {
  std::vector<SequenceFactEntry> entries;
  for (const auto& view : index.formatFacts<SequenceProgramAsset>(kAkaoFormatName, "akao.sequence")) {
    const auto sequenceId = fieldU32(view.payload, "sequence_id");
    const auto offset = fieldU32(view.payload, "offset").value_or(0);
    if (!sequenceId) {
      continue;
    }
    entries.push_back(SequenceFactEntry{
        .asset = view.asset.metadata.id,
        .name = view.asset.metadata.name,
        .sourceId = view.fact.scope.source,
        .source = view.source,
        .sequenceId = *sequenceId,
        .sampleSetId = fieldU32(view.payload, "sample_set_id"),
        .offset = offset,
    });
  }
  std::ranges::sort(entries, {}, [](const SequenceFactEntry& entry) { return entry.asset.value; });
  return entries;
}

[[nodiscard]] std::vector<InstrumentFactEntry> instrumentFacts(const MatchFactIndex& index) {
  std::vector<InstrumentFactEntry> entries;
  for (const auto& view : index.formatFacts<InstrumentSetAsset>(kAkaoFormatName, "akao.instrument-set")) {
    const auto sequenceId = fieldU32(view.payload, "sequence_id");
    if (!sequenceId) {
      continue;
    }
    entries.push_back(InstrumentFactEntry{
        .asset = view.asset.metadata.id,
        .sourceId = view.fact.scope.source,
        .sequenceId = *sequenceId,
    });
  }
  std::ranges::sort(entries, {}, [](const InstrumentFactEntry& entry) { return entry.asset.value; });
  return entries;
}

[[nodiscard]] std::vector<SampleFactEntry> sampleFacts(const MatchFactIndex& index) {
  std::vector<SampleFactEntry> entries;
  for (const auto& view : index.formatFacts<SampleCollectionAsset>(kAkaoFormatName, "akao.sample-collection")) {
    const auto firstArt = fieldU32(view.payload, "first_art");
    const auto artCount = fieldU32(view.payload, "art_count");
    if (!firstArt || !artCount) {
      continue;
    }
    entries.push_back(SampleFactEntry{
        .asset = view.asset.metadata.id,
        .sourceId = view.fact.scope.source,
        .source = view.source,
        .sampleSetId = fieldU32(view.payload, "sample_set_id"),
        .firstArt = *firstArt,
        .artCount = *artCount,
        .scanOrdinal = fieldU32(view.payload, "scan_ordinal").value_or(0),
    });
  }
  std::ranges::sort(entries, {}, &SampleFactEntry::scanOrdinal);
  return entries;
}

[[nodiscard]] std::map<u32, std::set<u32>> requiredArtFacts(const MatchFactIndex& index) {
  std::map<u32, std::set<u32>> requiredByInstrument;
  for (const auto& view : index.formatFacts<InstrumentSetAsset>(kAkaoFormatName, "akao.required-articulation")) {
    const auto art = fieldU32(view.payload, "art_id");
    if (!art || *art == 0) {
      continue;
    }
    requiredByInstrument[view.asset.metadata.id.value].insert(*art);
  }
  return requiredByInstrument;
}

[[nodiscard]] std::optional<InstrumentFactEntry> chooseInstrument(const SequenceFactEntry& sequence,
                                                                  const std::vector<InstrumentFactEntry>& instruments) {
  std::optional<InstrumentFactEntry> fallback;
  for (const auto& instrument : instruments) {
    if (instrument.sequenceId != sequence.sequenceId) {
      continue;
    }
    fallback = instrument;
    if (sameSource(sequence.sourceId, instrument.sourceId)) {
      return instrument;
    }
  }
  return fallback;
}

[[nodiscard]] std::vector<SampleFactEntry> candidateSamples(const SequenceFactEntry& sequence,
                                                           const std::vector<SampleFactEntry>& samples) {
  std::vector<SampleFactEntry> candidates;
  const bool isolated = psfLike(sequence.source);
  for (const auto& sample : samples) {
    if (isolated && !sameSource(sequence.sourceId, sample.sourceId)) {
      continue;
    }
    candidates.push_back(sample);
  }
  std::ranges::sort(candidates, std::ranges::greater{}, &SampleFactEntry::scanOrdinal);
  return candidates;
}

[[nodiscard]] std::vector<DesiredCollection> resolveAkaoCollections(const MatchContext& context) {
  const MatchFactIndex index(context);
  const auto sequences = sequenceFacts(index);
  const auto instruments = instrumentFacts(index);
  const auto samples = sampleFacts(index);
  const auto requiredByInstrument = requiredArtFacts(index);

  std::vector<DesiredCollection> collections;
  for (const auto& sequence : sequences) {
    CollectionAssembly collection(collectionKey(sequence), sequence.name.empty() ? "Akao Collection" : sequence.name);
    collection.sequence(sequence.asset);

    const auto instrument = chooseInstrument(sequence, instruments);
    if (!instrument) {
      collection.requireInstrumentSet();
      collections.push_back(std::move(collection).finish());
      continue;
    }
    collection.instrumentSet(instrument->asset);

    std::set<u32> remaining;
    if (auto found = requiredByInstrument.find(instrument->asset.value); found != requiredByInstrument.end()) {
      remaining = found->second;
    }

    auto candidates = candidateSamples(sequence, samples);
    std::vector<SampleFactEntry> selected;
    if (sequence.sampleSetId && *sequence.sampleSetId > 0) {
      auto preferred = std::ranges::find_if(candidates, [&](const SampleFactEntry& sample) {
        return sample.sampleSetId && *sample.sampleSetId == *sequence.sampleSetId;
      });
      if (preferred != candidates.end()) {
        selected.push_back(*preferred);
        for (auto it = remaining.begin(); it != remaining.end();) {
          if (covers(*preferred, *it)) {
            it = remaining.erase(it);
          } else {
            ++it;
          }
        }
      } else if (!psfLike(sequence.source)) {
        collection.incomplete(CollectionIssue{
            .severity = Severity::Warning,
            .code = "missing-preferred-sample-set",
            .message = missingSampleMessage(sequence),
            .asset = sequence.asset,
        });
      }
    }

    for (const auto& sample : candidates) {
      if (std::ranges::find(selected, sample.asset, &SampleFactEntry::asset) != selected.end()) {
        continue;
      }
      const bool associated = sameSampleSet(sequence.sampleSetId, sample.sampleSetId);
      const bool matchesRequired = std::ranges::any_of(remaining, [&](u32 art) { return covers(sample, art); });
      if (!associated && !matchesRequired) {
        continue;
      }
      selected.push_back(sample);
      for (auto it = remaining.begin(); it != remaining.end();) {
        if (covers(sample, *it)) {
          it = remaining.erase(it);
        } else {
          ++it;
        }
      }
      if (remaining.empty() && !selected.empty()) {
        break;
      }
    }

    std::ranges::sort(selected, {}, &SampleFactEntry::firstArt);
    for (const auto& sample : selected) {
      collection.sampleCollection(sample.asset);
    }
    if (selected.empty()) {
      collection.incomplete(CollectionIssue{
          .severity = Severity::Warning,
          .code = "missing-sample-collection",
          .message = missingSampleMessage(sequence),
          .asset = sequence.asset,
      });
    } else if (!remaining.empty()) {
      std::string message = "Akao sample collections do not cover required articulation ids:";
      for (const u32 art : remaining) {
        message += " " + std::to_string(art);
      }
      collection.incomplete(CollectionIssue{
          .severity = Severity::Warning,
          .code = "missing-articulation-coverage",
          .message = std::move(message),
          .asset = instrument->asset,
      });
    }
    collection.requireSequence().requireInstrumentSet().requireSampleCollection();
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

[[nodiscard]] bool canScanAkao(const SourceFile& source, std::span<const u8> bytes) {
  ByteReader reader(source.id, bytes);
  if (reader.size() >= 0x1a8000 && reader.has(0xe0000, 4) && reader.has(0x156000, 4) &&
      reader.le32(0xe0000) == 0x1010 && reader.le32(0x156000) == 0x1010) {
    return true;
  }
  for (u64 offset = 0; offset + 0x10 <= reader.size(); ++offset) {
    if (reader.be32(offset) == kAkaoSignature) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::vector<u32> akaoOffsets(ByteReader reader) {
  std::vector<u32> offsets;
  for (u64 offset = 0; offset + 0x10 <= reader.size(); ++offset) {
    if (reader.be32(offset) == kAkaoSignature) {
      offsets.push_back(static_cast<u32>(offset));
    }
  }
  return offsets;
}

void addSampleFacts(ScanResultBuilder& result, const AkaoSampleCollectionParse& parsed) {
  std::vector<MatchField> fields{
      MatchField{.name = "first_art", .value = std::to_string(parsed.firstArtId)},
      MatchField{.name = "art_count", .value = std::to_string(parsed.artCount)},
      MatchField{.name = "scan_ordinal", .value = std::to_string(parsed.scanOrdinal)},
  };
  addOptionalField(fields, "sample_set_id", parsed.sampleSetId ? std::optional<u32>{*parsed.sampleSetId} : std::nullopt);
  result.sourceFact(parsed.ref.id, akaoFact("akao.sample-collection", std::move(fields)));
}

void addSequenceFacts(ScanResultBuilder& result, ScanSequenceRef sequence, const AkaoSequenceAnalysis& analysis) {
  std::vector<MatchField> fields{
      MatchField{.name = "sequence_id", .value = std::to_string(analysis.header.sequenceId)},
      MatchField{.name = "offset", .value = std::to_string(analysis.header.offset)},
      MatchField{.name = "version", .value = versionName(analysis.header.version)},
  };
  addOptionalField(fields, "sample_set_id",
                   analysis.header.sampleSetId ? std::optional<u32>{*analysis.header.sampleSetId} : std::nullopt);
  result.sourceFact(sequence.id, akaoFact("akao.sequence", std::move(fields)));
}

void addInstrumentFacts(ScanResultBuilder& result, ScanInstrumentSetRef instrument, const AkaoSequenceAnalysis& analysis,
                        std::span<const u32> requiredArts) {
  result.sourceFact(instrument.id, akaoFact("akao.instrument-set", {
                                           MatchField{.name = "sequence_id",
                                                      .value = std::to_string(analysis.header.sequenceId)},
                                       }));
  for (const u32 artId : requiredArts) {
    if (artId == 0) {
      continue;
    }
    result.sourceFact(instrument.id, akaoFact("akao.required-articulation", {
                                             MatchField{.name = "sequence_id",
                                                        .value = std::to_string(analysis.header.sequenceId)},
                                             MatchField{.name = "art_id", .value = std::to_string(artId)},
                                         }));
  }
}

[[nodiscard]] ScanResult scanAkao(const ScanInput& input) {
  ScanResultBuilder result(input, std::string(kAkaoFormatName), std::string(kAkaoCollectionResolver));
  const AkaoPs1Version sourceVersion = determineVersionFromSource(input.source);
  std::vector<AkaoSampleCollectionParse> sampleCollections;
  u32 scanOrdinal = 0;

  for (const u32 offset : akaoOffsets(input.reader)) {
    if (input.reader.le16(offset + 6) != 0 || !isPossibleAkaoSampleCollection(input.reader, offset)) {
      continue;
    }
    AkaoPs1Version version = sourceVersion == AkaoPs1Version::Unknown ? guessSampleVersion(input.reader, offset)
                                                                       : sourceVersion;
    auto ref = result.reserveSampleCollection();
    if (auto parsed = parseAkaoSampleCollection(input, result, ref, offset, version, scanOrdinal++)) {
      addSampleFacts(result, *parsed);
      sampleCollections.push_back(std::move(*parsed));
    } else {
      result.warning("Akao sample collection header was detected but sample data could not be parsed",
                     input.reader.range(offset, 0x40));
    }
  }

  if (input.reader.size() >= 0x1a8000 && input.reader.has(0xe0000, 4) && input.reader.has(0x156000, 4) &&
      input.reader.le32(0xe0000) == 0x1010 && input.reader.le32(0x156000) == 0x1010) {
    auto ref = result.reserveSampleCollection();
    if (auto parsed = parseAkaoSampleCollection(
            input, result, ref,
            AkaoInstrDatLocation{.instrAllOffset = 0xe0000, .instrDatOffset = 0x156000, .firstArtId = 0, .artCount = 128},
            scanOrdinal++)) {
      addSampleFacts(result, *parsed);
      sampleCollections.push_back(std::move(*parsed));
    }
  }

  const AkaoArtMap artMap = buildAkaoArtMap(sampleCollections);
  for (const u32 offset : akaoOffsets(input.reader)) {
    if (input.reader.le16(offset + 6) == 0) {
      continue;
    }
    auto analysis = analyzeAkaoSequence(input.reader, input.source, offset);
    if (!analysis) {
      continue;
    }

    const auto sequenceRef = result.reserveSequence();
    const auto instrumentRef = result.reserveInstrumentSet();
    static_cast<void>(result.sequence(sequenceRef, [&](AssetId id) {
      std::vector<Diagnostic> diagnostics;
      auto asset =
          parseAkaoSequenceProgram(input, id, *analysis, instrumentRef, &result.sourceMap(), &diagnostics);
      for (auto& diagnostic : diagnostics) {
        result.diagnostic(std::move(diagnostic));
      }
      return asset;
    }));
    static_cast<void>(result.instrumentSet(
        instrumentRef, [&](AssetId id) { return parseAkaoInstrumentSet(input, id, *analysis, artMap); }));

    const auto required = requiredArticulations(input.reader, *analysis);
    addSequenceFacts(result, sequenceRef, *analysis);
    addInstrumentFacts(result, instrumentRef, *analysis, required);
  }

  return result.finish();
}

}  // namespace

void registerAkaoModule(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = std::string(kAkaoFormatName),
      .canScan = canScanAkao,
      .scan = scanAkao,
      .collectionResolverId = std::string(kAkaoCollectionResolver),
      .resolveCollections = resolveAkaoCollections,
  });
}

}  // namespace vgmtrans::formats::akao
