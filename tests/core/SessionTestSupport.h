/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "SequenceTestSupport.h"

#include "value/scan/CollectionDiscovery.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/session/Session.h"

namespace {

// Tiny magic-byte formats isolate session/registry behavior from real decoders.
[[nodiscard]] bool hasProbeMagic(const auto& input, u8 magic, u64 minimumSize = 1) {
  return input.reader.size() >= minimumSize && input.reader.u8At(0) == magic;
}

[[nodiscard]] SourceExtractor probeSequenceContainerExtractor() {
  return SourceExtractor{
      .name = "ProbeSequenceContainer",
      .extract = [](const ExtractionInput& input) -> ExtractionResult {
        if (input.source.derived() || !hasProbeMagic(input, 0xaa)) {
          return {};
        }
        const auto bytes = input.reader.slice(0, input.reader.size());
        return ExtractionResult{
            .sources = {ExtractedSource{
                .file =
                    SourceFile{
                        .name = input.source.name + ".child",
                        .origin = input.reader.range(0, 1),
                        .knownFormat = "probe-sequence",
                    },
                .bytes = std::vector<u8>(bytes.begin(), bytes.end()),
            }},
        };
      },
  };
}

[[nodiscard]] ScanResult scanProbeSequence(const ScanInput& input) {
  if (!hasProbeMagic(input, 0xaa)) {
    return {};
  }

  const auto assetId = input.ids.nextAssetId();
  const auto assetRange = input.reader.range(0, input.reader.size());
  SourceMapBuilder sourceMap([&input]() { return input.ids.nextSourceAnnotationId(); });
  auto root = sourceMap.annotation(SourceRole::Sequence, input.source.name, assetRange)
                  .kind("probe-sequence")
                  .owner(ObjectRefs::sequence(assetId));
  sourceMap.header("Header", input.reader.range(0, 1)).kind("probe-header").parent(root.id());

  SequenceProgramAsset sequence{
      .metadata =
          AssetMetadata{
              .id = assetId,
              .format = "ProbeSequence",
              .name = input.source.name,
              .range = assetRange,
          },
      .program = probeSequenceProgram(),
  };

  ScanResult result;
  result.assets.emplace_back(std::move(sequence));
  result.sourceMap = sourceMap.finish();
  result.explicitCollections.push_back(ExplicitCollection{
      .key =
          CollectionKey{
              .resolver = "ProbeSequence",
              .value = "source:" + std::to_string(input.source.id.value),
          },
      .name = input.source.name,
      .members = {.sequence = assetId},
  });
  result.diagnostics.push_back(Diagnostic{
      .severity = Severity::Info,
      .message = "probe sequence scanned",
      .range = assetRange,
  });

  return result;
}

[[nodiscard]] FormatModule probeSequenceModule() {
  return FormatModule{
      .name = "ProbeSequence",
      .scan = scanProbeSequence,
  };
}

[[nodiscard]] ScanResult scanProbeMisc(const ScanInput& input) {
  if (!input.source.derived() || !hasProbeMagic(input, 0xbb)) {
    return {};
  }

  ScanResultBuilder out(input, "ProbeMisc");
  const SourceRange range = input.reader.range(0, input.reader.size());
  const auto asset = out.misc(input.source.name, range).payload({input.reader.u8At(0), input.reader.u8At(1)});
  out.sourceMap().annotation(SourceRole::Payload, input.source.name, range).owner(ObjectRefs::misc(asset.id()));
  return out.finish();
}

[[nodiscard]] FormatModule probeMiscModule() {
  return FormatModule{
      .name = "ProbeMisc",
      .scan = scanProbeMisc,
  };
}

[[nodiscard]] ScanResult scanProbeExplicitCollection(const ScanInput& input) {
  if (!hasProbeMagic(input, 0xab)) {
    return {};
  }

  ScanResultBuilder out(input, "ProbeExplicit");
  const auto sequence = out.sequence("Explicit Sequence", input.reader.range(0, 1)).program(probeSequenceProgram());
  out.sourceMap()
      .header("Probe Header", input.reader.range(0, 1))
      .owner(ObjectRefs::sequence(sequence.id()))
      .field("Magic", input.reader.range(0, 1), input.reader.u8At(0), SourceValueDisplay::Hex);
  if (input.reader.size() > 1) {
    out.sourceMap()
        .annotation(SourceRole::Payload, "Probe Payload", input.reader.range(1, input.reader.size() - 1))
        .owner(ObjectRefs::sequence(sequence.id()));
  }
  out.collection(input.source.name,
                 CollectionKey{.resolver = "ProbeExplicit", .value = "source:" + std::to_string(input.source.id.value)})
      .sequence(sequence);
  return out.finish();
}

[[nodiscard]] FormatModule probeExplicitCollectionModule() {
  return FormatModule{
      .name = "ProbeExplicit",
      .scan = scanProbeExplicitCollection,
  };
}

struct ProbeBankData {
  u32 bank = 0;
};

[[nodiscard]] ScanResult scanProbeBankSequence(const ScanInput& input) {
  if (!hasProbeMagic(input, 0xcc, 2)) {
    return {};
  }

  const auto assetId = input.ids.nextAssetId();
  const auto bank = input.reader.u8At(1);
  SequenceProgramAsset sequence{
      .metadata =
          AssetMetadata{
              .id = assetId,
              .format = "ProbeBank",
              .name = input.source.name,
              .range = input.reader.range(0, input.reader.size()),
          },
      .program = probeSequenceProgram(),
      .privateData = AssetPrivateData::make(ProbeBankData{.bank = bank}),
  };

  ScanResult result;
  result.assets.emplace_back(std::move(sequence));
  result.sourceMap = SourceMap{{SourceAnnotation{
      .id = input.ids.nextSourceAnnotationId(),
      .range = input.reader.range(0, input.reader.size()),
      .role = SourceRole::Sequence,
      .label = input.source.name,
      .owner = ObjectRefs::sequence(assetId),
  }}};
  return result;
}

[[nodiscard]] ScanResult scanProbeBankInstruments(const ScanInput& input) {
  if (!hasProbeMagic(input, 0xdd, 2)) {
    return {};
  }

  const auto assetId = input.ids.nextAssetId();
  const auto bank = input.reader.u8At(1);
  ScanResult result;
  result.assets.emplace_back(SoundBankAsset{
      .metadata =
          AssetMetadata{
              .id = assetId,
              .format = "ProbeBank",
              .name = input.source.name,
              .range = input.reader.range(0, input.reader.size()),
          },
      .privateData = AssetPrivateData::make(ProbeBankData{.bank = bank}),
  });
  result.sourceMap = SourceMap{{SourceAnnotation{
      .id = input.ids.nextSourceAnnotationId(),
      .range = input.reader.range(0, input.reader.size()),
      .role = SourceRole::SoundBank,
      .label = input.source.name,
      .owner = ObjectRefs::asset(assetId),
  }}};
  return result;
}

[[nodiscard]] std::vector<DesiredCollection> resolveProbeBankCollections(const CollectionDiscoveryContext& context) {
  std::vector<DesiredCollection> collections;
  const auto attach = [&](const auto& entry) {
    const std::string key = "bank:" + std::to_string(entry.data->bank);
    auto found = std::ranges::find_if(collections,
                                      [&](const DesiredCollection& collection) { return collection.localKey == key; });
    if (found == collections.end()) {
      collections.push_back(DesiredCollection{
          .localKey = key,
          .name = "Probe Bank " + std::to_string(entry.data->bank),
      });
      found = std::prev(collections.end());
    }
    return found;
  };
  for (const auto& sequence : context.assetsWithData<SequenceProgramAsset, ProbeBankData>()) {
    attach(sequence)->members.sequence = sequence.id();
  }
  for (const auto& bank : context.assetsWithData<SoundBankAsset, ProbeBankData>()) {
    attach(bank)->members.soundBanks.push_back(bank.id());
  }
  for (auto& collection : collections) {
    if (!collection.members.sequence) {
      collection.issues.push_back(missingSequenceIssue());
    }
    if (collection.members.soundBanks.empty()) {
      collection.issues.push_back(missingSoundBankIssue());
    }
  }
  return collections;
}

[[nodiscard]] FormatModule probeBankSequenceModule() {
  return FormatModule{
      .name = "ProbeBankSequence",
      .scan = scanProbeBankSequence,
      .collectionResolverId = "ProbeBank",
      .resolveCollections = resolveProbeBankCollections,
  };
}

[[nodiscard]] FormatModule probeBankInstrumentModule() {
  return FormatModule{
      .name = "ProbeBankInstrument",
      .scan = scanProbeBankInstruments,
  };
}

[[nodiscard]] ScanResult scanProbeDuplicateAssets(const ScanInput& input) {
  if (!hasProbeMagic(input, 0xee)) {
    return {};
  }

  return ScanResult{
      .assets = {MiscAsset{
                     .metadata =
                         AssetMetadata{
                             .id = AssetId{7},
                             .format = "ProbeDuplicate",
                             .name = "First duplicate",
                             .range = input.reader.range(0, input.reader.size()),
                         },
                 },
                 MiscAsset{
                     .metadata =
                         AssetMetadata{
                             .id = AssetId{7},
                             .format = "ProbeDuplicate",
                             .name = "Second duplicate",
                             .range = input.reader.range(0, input.reader.size()),
                         },
                 }},
  };
}

[[nodiscard]] FormatModule probeDuplicateAssetModule() {
  return FormatModule{
      .name = "ProbeDuplicate",
      .scan = scanProbeDuplicateAssets,
  };
}

[[nodiscard]] ExtractionResult extractProbeBadSource(const ExtractionInput& input) {
  if (!hasProbeMagic(input, 0xf1)) {
    return {};
  }

  return ExtractionResult{
      .sources = {ExtractedSource{
          .file =
              SourceFile{
                  .name = "bad-parent.child",
                  .origin = SourceRange{.source = SourceId{99}, .offset = 0, .size = 1},
              },
          .bytes = {0xbb},
      }},
  };
}

[[nodiscard]] SourceExtractor probeBadSourceExtractor() {
  return SourceExtractor{
      .name = "ProbeBadExtracted",
      .extract = extractProbeBadSource,
  };
}

[[nodiscard]] ScanResult scanNothing(const ScanInput&) {
  return {};
}

[[nodiscard]] std::vector<DesiredCollection> fragileProbeSequenceResolver(const CollectionDiscoveryContext& context) {
  const auto sequences = context.assets<SequenceProgramAsset>();
  if (sequences.empty() || sequences.size() > 1) {
    throw std::runtime_error("resolver exploded");
  }
  return {DesiredCollection{
      .localKey = "dynamic",
      .name = "Dynamic Probe Collection",
      .members = {.sequence = sequences.front()->metadata.id},
  }};
}

[[nodiscard]] FormatModule fragileProbeSequenceModule() {
  return FormatModule{
      .name = "ProbeSequenceFragileResolver",
      .scan = scanProbeSequence,
      .collectionResolverId = "ProbeSequence",
      .resolveCollections = fragileProbeSequenceResolver,
  };
}

[[nodiscard]] std::vector<DesiredCollection> missingAssetCollectionResolver(const CollectionDiscoveryContext&) {
  return {DesiredCollection{
      .localKey = "missing-assets",
      .name = "Missing Assets",
      .members =
          {
              .sequence = AssetId{99},
              .soundBanks = {AssetId{98}},
              .samplePools = {AssetId{97}},
              .miscAssets = {AssetId{96}},
          },
  }};
}

[[nodiscard]] FormatModule missingAssetCollectionResolverModule() {
  return FormatModule{
      .name = "ProbeMissingRefs",
      .scan = scanNothing,
      .resolveCollections = missingAssetCollectionResolver,
  };
}

[[nodiscard]] std::vector<DesiredCollection> wrongTypeCollectionResolver(const CollectionDiscoveryContext& context) {
  std::optional<AssetId> sequence;
  for (const auto* asset : context.assets<SequenceProgramAsset>()) {
    sequence = asset->metadata.id;
    break;
  }
  if (!sequence) {
    return {};
  }

  return {DesiredCollection{
      .localKey = "wrong-type-assets",
      .name = "Wrong Type Assets",
      .members =
          {
              .soundBanks = {*sequence},
              .samplePools = {*sequence},
              .miscAssets = {*sequence},
          },
  }};
}

[[nodiscard]] FormatModule wrongTypeCollectionResolverModule() {
  return FormatModule{
      .name = "ProbeWrongTypeRefs",
      .scan = scanNothing,
      .resolveCollections = wrongTypeCollectionResolver,
  };
}

[[nodiscard]] std::vector<DesiredCollection> duplicateKeyCollectionResolver(const CollectionDiscoveryContext&) {
  return {DesiredCollection{
              .localKey = "same-key",
              .name = "First",
          },
          DesiredCollection{
              .localKey = "same-key",
              .name = "Second",
          }};
}

[[nodiscard]] FormatModule duplicateKeyCollectionResolverModule() {
  return FormatModule{
      .name = "ProbeDuplicateKeys",
      .scan = scanNothing,
      .resolveCollections = duplicateKeyCollectionResolver,
  };
}

}  // namespace
