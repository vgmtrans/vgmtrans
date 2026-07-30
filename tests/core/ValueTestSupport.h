/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/Export.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/scan/CollectionResolver.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/base/LevelScale.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/ModulationAnalysis.h"
#include "value/sequence/CompiledCommandDialect.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SampleDecoder.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/SynthExportData.h"
#include "value/export/audio/WavExporter.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace vgmtrans::core;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

u32 readLe32(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<u32>(bytes[offset]) | (static_cast<u32>(bytes[offset + 1]) << 8) |
         (static_cast<u32>(bytes[offset + 2]) << 16) | (static_cast<u32>(bytes[offset + 3]) << 24);
}

u16 readLe16(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<u16>(bytes[offset]) | (static_cast<u16>(bytes[offset + 1]) << 8);
}

s16 readLeS16(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<s16>(readLe16(bytes, offset));
}

s32 readLeS32(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<s32>(readLe32(bytes, offset));
}

bool containsAscii(const std::vector<u8>& bytes, std::string_view text) {
  return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}

size_t asciiOffset(const std::vector<u8>& bytes, std::string_view text) {
  const auto found = std::search(bytes.begin(), bytes.end(), text.begin(), text.end());
  if (found == bytes.end()) {
    throw std::runtime_error("expected ASCII marker was not found");
  }
  return static_cast<size_t>(std::distance(bytes.begin(), found));
}

u32 chunkSize(const std::vector<u8>& bytes, std::string_view chunkId) {
  return readLe32(bytes, asciiOffset(bytes, chunkId) + 4);
}

bool soundFontInfoChunksHaveEvenDeclaredSizes(const std::vector<u8>& bytes) {
  const auto infoTypeOffset = asciiOffset(bytes, "INFO");
  if (infoTypeOffset < 8) {
    return false;
  }

  const std::string_view listId(reinterpret_cast<const char*>(bytes.data() + infoTypeOffset - 8), 4);
  if (listId != "LIST") {
    return false;
  }

  const size_t infoListSize = readLe32(bytes, infoTypeOffset - 4);
  const size_t infoEnd = infoTypeOffset + infoListSize;
  if (infoEnd > bytes.size()) {
    return false;
  }
  size_t offset = infoTypeOffset + 4;
  while (offset + 8 <= infoEnd) {
    const u32 size = readLe32(bytes, offset + 4);
    if ((size & 1u) != 0) {
      return false;
    }
    offset += 8 + size + (size & 1u);
  }
  return offset == infoEnd;
}

bool soundFontIgenContainsAmount(const std::vector<u8>& bytes, u16 generator, s16 expectedAmount) {
  const auto chunkOffset = asciiOffset(bytes, "igen");
  const auto size = chunkSize(bytes, "igen");
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 4 <= payloadOffset + size; offset += 4) {
    if (readLe16(bytes, offset) == generator && readLeS16(bytes, offset + 2) == expectedAmount) {
      return true;
    }
  }
  return false;
}

bool soundFontPgenContainsAmount(const std::vector<u8>& bytes, u16 generator, s16 expectedAmount) {
  const auto chunkOffset = asciiOffset(bytes, "pgen");
  const auto size = chunkSize(bytes, "pgen");
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 4 <= payloadOffset + size; offset += 4) {
    if (readLe16(bytes, offset) == generator && readLeS16(bytes, offset + 2) == expectedAmount) {
      return true;
    }
  }
  return false;
}

bool soundFontBagAt(const std::vector<u8>& bytes, std::string_view chunkId, size_t index, u16 genIndex, u16 modIndex) {
  const auto chunkOffset = asciiOffset(bytes, chunkId);
  const auto size = chunkSize(bytes, chunkId);
  const auto offset = chunkOffset + 8 + (index * 4);
  if (offset + 4 > chunkOffset + 8 + size) {
    return false;
  }

  return readLe16(bytes, offset) == genIndex && readLe16(bytes, offset + 2) == modIndex;
}

bool soundFontImodContains(const std::vector<u8>& bytes, u16 source, u16 destination, s16 amount) {
  const auto chunkOffset = asciiOffset(bytes, "imod");
  const auto size = chunkSize(bytes, "imod");
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 10 <= payloadOffset + size; offset += 10) {
    if (readLe16(bytes, offset) == source && readLe16(bytes, offset + 2) == destination &&
        readLeS16(bytes, offset + 4) == amount) {
      return true;
    }
  }
  return false;
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 destination, s32 expectedScale) {
  const auto chunkOffset = asciiOffset(bytes, "art2");
  const auto payloadOffset = chunkOffset + 8;
  const auto connectionCount = readLe32(bytes, payloadOffset + 4);
  for (u32 i = 0; i < connectionCount; ++i) {
    const auto offset = payloadOffset + 8 + (static_cast<size_t>(i) * 12);
    if (readLe16(bytes, offset + 4) == destination && readLeS32(bytes, offset + 8) == expectedScale) {
      return true;
    }
  }
  return false;
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 source, u16 destination, s32 expectedScale) {
  const auto chunkOffset = asciiOffset(bytes, "art2");
  const auto payloadOffset = chunkOffset + 8;
  const auto connectionCount = readLe32(bytes, payloadOffset + 4);
  for (u32 i = 0; i < connectionCount; ++i) {
    const auto offset = payloadOffset + 8 + (static_cast<size_t>(i) * 12);
    if (readLe16(bytes, offset) == source && readLe16(bytes, offset + 4) == destination &&
        readLeS32(bytes, offset + 8) == expectedScale) {
      return true;
    }
  }
  return false;
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 source, u16 control, u16 destination,
                               s32 expectedScale) {
  const auto chunkOffset = asciiOffset(bytes, "art2");
  const auto payloadOffset = chunkOffset + 8;
  const auto connectionCount = readLe32(bytes, payloadOffset + 4);
  for (u32 i = 0; i < connectionCount; ++i) {
    const auto offset = payloadOffset + 8 + (static_cast<size_t>(i) * 12);
    if (readLe16(bytes, offset) == source && readLe16(bytes, offset + 2) == control &&
        readLe16(bytes, offset + 4) == destination && readLeS32(bytes, offset + 8) == expectedScale) {
      return true;
    }
  }
  return false;
}

bool sameRange(SourceRange lhs, SourceRange rhs) {
  return lhs.source == rhs.source && lhs.offset == rhs.offset && lhs.size == rhs.size;
}

const Diagnostic& diagnosticWithMessage(const std::vector<Diagnostic>& diagnostics, std::string_view message) {
  const auto found = std::ranges::find_if(
      diagnostics, [message](const Diagnostic& diagnostic) { return diagnostic.message == message; });
  if (found == diagnostics.end()) {
    throw std::runtime_error("expected diagnostic was not found");
  }
  return *found;
}

void expectDiagnosticRange(const std::vector<Diagnostic>& diagnostics, std::string_view message,
                           SourceRange expectedRange) {
  const auto& diagnostic = diagnosticWithMessage(diagnostics, message);
  expect(diagnostic.range.has_value(), "diagnostic should preserve a source range");
  expect(sameRange(*diagnostic.range, expectedRange), "diagnostic should preserve the expected source range");
}

[[nodiscard]] bool canScanProbeSequence(const SourceFile&, std::span<const u8> bytes) {
  return !bytes.empty() && bytes[0] == 0xaa;
}

[[nodiscard]] ScanResult scanProbeSequence(const ScanInput& input) {
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
      .program =
          SequenceProgram{
              .dialect = DialectId{.value = "probe"},
              .timebase = Timebase{.ppqn = 48},
          },
  };

  ScanResult result;
  result.assets.emplace_back(std::move(sequence));
  result.sourceMap = sourceMap.finish();
  result.matchFacts.push_back(MatchFact{
      .asset = assetId,
      .format = "ProbeSequence",
      .scope = MatchScope{.kind = MatchScopeKind::Source, .source = input.source.id},
      .payload =
          CollectionMemberFact{
              .key = CollectionKey{.resolver = "ProbeSequence",
                                   .value = "source:" + std::to_string(input.source.id.value)},
              .collectionName = input.source.name,
              .role = CollectionMemberRole::Sequence,
          },
  });
  result.diagnostics.push_back(Diagnostic{
      .severity = Severity::Info,
      .message = "probe sequence scanned",
      .range = assetRange,
  });

  if (!input.source.derived()) {
    result.extractedSources.push_back(ExtractedSource{
        .file = SourceFile{.name = input.source.name + ".child"},
        .bytes = {0xbb, 0x01},
        .origin = input.reader.range(0, 1),
    });
  }

  return result;
}

[[nodiscard]] std::vector<DesiredCollection> resolveProbeSequenceCollections(const MatchContext& context) {
  return resolveCollectionMemberFacts(context, "ProbeSequence", "ProbeSequence");
}

[[nodiscard]] FormatModule probeSequenceModule() {
  return FormatModule{
      .name = "ProbeSequence",
      .canScan = canScanProbeSequence,
      .scan = scanProbeSequence,
      .resolveCollections = resolveProbeSequenceCollections,
  };
}

[[nodiscard]] bool canScanProbeMisc(const SourceFile& source, std::span<const u8> bytes) {
  return source.derived() && !bytes.empty() && bytes[0] == 0xbb;
}

[[nodiscard]] ScanResult scanProbeMisc(const ScanInput& input) {
  ScanResultBuilder out(input, "ProbeMisc");
  const SourceRange range = input.reader.range(0, input.reader.size());
  const auto asset = out.misc(input.source.name, range).payload({input.reader.u8At(0), input.reader.u8At(1)});
  out.sourceMap().annotation(SourceRole::Payload, input.source.name, range).owner(ObjectRefs::misc(asset.id));
  return out.finish();
}

[[nodiscard]] FormatModule probeMiscModule() {
  return FormatModule{
      .name = "ProbeMisc",
      .canScan = canScanProbeMisc,
      .scan = scanProbeMisc,
  };
}

[[nodiscard]] bool canScanProbeExplicitCollection(const SourceFile&, std::span<const u8> bytes) {
  return !bytes.empty() && bytes[0] == 0xab;
}

[[nodiscard]] ScanResult scanProbeExplicitCollection(const ScanInput& input) {
  ScanResultBuilder out(input, "ProbeExplicit");
  const auto sequence = out.sequence("Explicit Sequence", input.reader.range(0, 1))
                            .program(SequenceProgram{
                                .dialect = DialectId{.value = "probe"},
                                .timebase = Timebase{.ppqn = 48},
                            });
  out.sourceMap()
      .header("Probe Header", input.reader.range(0, 1))
      .owner(ObjectRefs::sequence(sequence.id))
      .field("Magic", input.reader.range(0, 1), input.reader.u8At(0), SourceValueDisplay::Hex);
  if (input.reader.size() > 1) {
    out.sourceMap()
        .annotation(SourceRole::Payload, "Probe Payload", input.reader.range(1, input.reader.size() - 1))
        .owner(ObjectRefs::sequence(sequence.id));
  }
  out.collection(input.source.name,
                 CollectionKey{.resolver = "ProbeExplicit", .value = "source:" + std::to_string(input.source.id.value)})
      .sequence(sequence);
  return out.finish();
}

[[nodiscard]] FormatModule probeExplicitCollectionModule() {
  return FormatModule{
      .name = "ProbeExplicit",
      .canScan = canScanProbeExplicitCollection,
      .scan = scanProbeExplicitCollection,
  };
}

[[nodiscard]] bool canScanProbeBankSequence(const SourceFile&, std::span<const u8> bytes) {
  return bytes.size() >= 2 && bytes[0] == 0xcc;
}

[[nodiscard]] bool canScanProbeBankInstruments(const SourceFile&, std::span<const u8> bytes) {
  return bytes.size() >= 2 && bytes[0] == 0xdd;
}

[[nodiscard]] MatchFact probeBankFact(const ScanInput& input, AssetId asset, u32 bank) {
  return MatchFact{
      .asset = asset,
      .format = "ProbeBank",
      .scope = MatchScope{.kind = MatchScopeKind::Session},
      .payload = IdMatchFact{.domain = "probe.bank", .value = bank},
  };
}

[[nodiscard]] ScanResult scanProbeBankSequence(const ScanInput& input) {
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
      .program =
          SequenceProgram{
              .dialect = DialectId{.value = "probe"},
              .timebase = Timebase{.ppqn = 48},
          },
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
  result.matchFacts.push_back(probeBankFact(input, assetId, bank));
  return result;
}

[[nodiscard]] ScanResult scanProbeBankInstruments(const ScanInput& input) {
  const auto assetId = input.ids.nextAssetId();
  const auto bank = input.reader.u8At(1);
  ScanResult result;
  result.assets.emplace_back(InstrumentSetAsset{
      .metadata =
          AssetMetadata{
              .id = assetId,
              .format = "ProbeBank",
              .name = input.source.name,
              .range = input.reader.range(0, input.reader.size()),
          },
  });
  result.sourceMap = SourceMap{{SourceAnnotation{
      .id = input.ids.nextSourceAnnotationId(),
      .range = input.reader.range(0, input.reader.size()),
      .role = SourceRole::InstrumentSet,
      .label = input.source.name,
      .owner = ObjectRefs::asset(assetId),
  }}};
  result.matchFacts.push_back(probeBankFact(input, assetId, bank));
  return result;
}

[[nodiscard]] std::vector<DesiredCollection> resolveProbeBankCollections(const MatchContext& context) {
  const MatchFactIndex index(context);
  std::vector<DesiredCollection> collections;
  for (const auto& fact : context.matchFacts()) {
    const auto* id = std::get_if<IdMatchFact>(&fact.payload);
    if (id == nullptr || fact.format != "ProbeBank" || id->domain != "probe.bank") {
      continue;
    }

    const CollectionKey key{.resolver = "ProbeBank", .value = "bank:" + std::to_string(id->value)};
    auto found =
        std::ranges::find_if(collections, [&](const DesiredCollection& collection) { return collection.key == key; });
    if (found == collections.end()) {
      collections.push_back(DesiredCollection{
          .key = key,
          .name = "Probe Bank " + std::to_string(id->value),
          .status = CollectionStatus::Incomplete,
      });
      found = std::prev(collections.end());
    }

    if (index.asset<SequenceProgramAsset>(fact.asset) != nullptr) {
      found->sequence = fact.asset;
    } else if (index.asset<InstrumentSetAsset>(fact.asset) != nullptr) {
      found->instrumentSets.push_back(fact.asset);
    }

    if (found->sequence && !found->instrumentSets.empty()) {
      found->status = CollectionStatus::Complete;
    }
  }
  for (auto& collection : collections) {
    if (!collection.sequence) {
      collection.status = CollectionStatus::Incomplete;
      collection.issues.push_back(missingSequenceIssue());
    }
    if (collection.instrumentSets.empty()) {
      collection.status = CollectionStatus::Incomplete;
      collection.issues.push_back(missingInstrumentSetIssue());
    }
  }
  return collections;
}

[[nodiscard]] FormatModule probeBankSequenceModule() {
  return FormatModule{
      .name = "ProbeBankSequence",
      .canScan = canScanProbeBankSequence,
      .scan = scanProbeBankSequence,
      .collectionResolverId = "ProbeBank",
      .resolveCollections = resolveProbeBankCollections,
  };
}

[[nodiscard]] FormatModule probeBankInstrumentModule() {
  return FormatModule{
      .name = "ProbeBankInstrument",
      .canScan = canScanProbeBankInstruments,
      .scan = scanProbeBankInstruments,
  };
}

[[nodiscard]] bool canScanProbeDuplicateAssets(const SourceFile&, std::span<const u8> bytes) {
  return !bytes.empty() && bytes[0] == 0xee;
}

[[nodiscard]] ScanResult scanProbeDuplicateAssets(const ScanInput& input) {
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
      .canScan = canScanProbeDuplicateAssets,
      .scan = scanProbeDuplicateAssets,
  };
}

[[nodiscard]] bool canScanProbeBadExtractedSource(const SourceFile&, std::span<const u8> bytes) {
  return !bytes.empty() && bytes[0] == 0xf1;
}

[[nodiscard]] ScanResult scanProbeBadExtractedSource(const ScanInput& input) {
  const auto assetId = input.ids.nextAssetId();
  ScanResult result;
  result.assets.emplace_back(MiscAsset{
      .metadata =
          AssetMetadata{
              .id = assetId,
              .format = "ProbeBadExtracted",
              .name = "Rejected asset",
              .range = input.reader.range(0, input.reader.size()),
          },
  });
  result.sourceMap = SourceMap{{SourceAnnotation{
      .id = input.ids.nextSourceAnnotationId(),
      .range = input.reader.range(0, input.reader.size()),
      .role = SourceRole::Payload,
      .label = "Rejected asset",
      .owner = ObjectRefs::misc(assetId),
  }}};
  result.extractedSources.push_back(ExtractedSource{
      .file = SourceFile{.name = "bad-parent.child"},
      .bytes = {0xbb},
      .origin = SourceRange{.source = SourceId{99}, .offset = 0, .size = 1},
  });
  return result;
}

[[nodiscard]] FormatModule probeBadExtractedSourceModule() {
  return FormatModule{
      .name = "ProbeBadExtracted",
      .canScan = canScanProbeBadExtractedSource,
      .scan = scanProbeBadExtractedSource,
  };
}

[[nodiscard]] bool canScanProbeBadFactAsset(const SourceFile&, std::span<const u8> bytes) {
  return !bytes.empty() && bytes[0] == 0xf2;
}

[[nodiscard]] ScanResult scanProbeBadFactAsset(const ScanInput& input) {
  return ScanResult{
      .matchFacts = {MatchFact{
          .asset = AssetId{99},
          .format = "ProbeBadFact",
          .scope = MatchScope{.kind = MatchScopeKind::Session},
          .payload = IdMatchFact{.domain = "probe.bad", .value = 1},
      }},
  };
}

[[nodiscard]] FormatModule probeBadFactAssetModule() {
  return FormatModule{
      .name = "ProbeBadFactAsset",
      .canScan = canScanProbeBadFactAsset,
      .scan = scanProbeBadFactAsset,
  };
}

[[nodiscard]] bool canScanProbeBadFactSource(const SourceFile&, std::span<const u8> bytes) {
  return !bytes.empty() && bytes[0] == 0xf3;
}

[[nodiscard]] ScanResult scanProbeBadFactSource(const ScanInput& input) {
  const auto assetId = input.ids.nextAssetId();
  return ScanResult{
      .assets = {MiscAsset{
          .metadata =
              AssetMetadata{
                  .id = assetId,
                  .format = "ProbeBadFact",
                  .name = "Bad source fact",
                  .range = input.reader.range(0, input.reader.size()),
              },
      }},
      .sourceMap = SourceMap{{SourceAnnotation{
          .id = input.ids.nextSourceAnnotationId(),
          .range = input.reader.range(0, input.reader.size()),
          .role = SourceRole::Payload,
          .label = "Bad source fact",
          .owner = ObjectRefs::misc(assetId),
      }}},
      .matchFacts = {MatchFact{
          .asset = assetId,
          .format = "ProbeBadFact",
          .scope = MatchScope{.kind = MatchScopeKind::Source, .source = SourceId{99}},
          .payload = IdMatchFact{.domain = "probe.bad", .value = 2},
      }},
  };
}

[[nodiscard]] FormatModule probeBadFactSourceModule() {
  return FormatModule{
      .name = "ProbeBadFactSource",
      .canScan = canScanProbeBadFactSource,
      .scan = scanProbeBadFactSource,
  };
}

[[nodiscard]] bool canScanNever(const SourceFile&, std::span<const u8>) {
  return false;
}

[[nodiscard]] ScanResult scanNothing(const ScanInput&) {
  return {};
}

[[nodiscard]] std::vector<DesiredCollection> fragileProbeSequenceResolver(const MatchContext& context) {
  if (context.assets().empty() || context.assets().size() > 1) {
    throw std::runtime_error("resolver exploded");
  }
  return resolveCollectionMemberFacts(context, "ProbeSequence", "ProbeSequence");
}

[[nodiscard]] FormatModule fragileProbeSequenceModule() {
  return FormatModule{
      .name = "ProbeSequenceFragileResolver",
      .canScan = canScanProbeSequence,
      .scan = scanProbeSequence,
      .collectionResolverId = "ProbeSequence",
      .resolveCollections = fragileProbeSequenceResolver,
  };
}

[[nodiscard]] std::vector<DesiredCollection> missingAssetCollectionResolver(const MatchContext&) {
  return {DesiredCollection{
      .key = CollectionKey{.resolver = "ProbeMissingRefs", .value = "missing-assets"},
      .name = "Missing Assets",
      .sequence = AssetId{99},
      .instrumentSets = {AssetId{98}},
      .sampleCollections = {AssetId{97}},
      .miscAssets = {AssetId{96}},
  }};
}

[[nodiscard]] FormatModule missingAssetCollectionResolverModule() {
  return FormatModule{
      .name = "ProbeMissingRefs",
      .canScan = canScanNever,
      .scan = scanNothing,
      .resolveCollections = missingAssetCollectionResolver,
  };
}

[[nodiscard]] std::vector<DesiredCollection> wrongTypeCollectionResolver(const MatchContext& context) {
  std::optional<AssetId> sequence;
  for (const auto& asset : context.assets()) {
    if (const auto* typed = std::get_if<SequenceProgramAsset>(&asset)) {
      sequence = typed->metadata.id;
      break;
    }
  }
  if (!sequence) {
    return {};
  }

  return {DesiredCollection{
      .key = CollectionKey{.resolver = "ProbeWrongTypeRefs", .value = "wrong-type-assets"},
      .name = "Wrong Type Assets",
      .instrumentSets = {*sequence},
      .sampleCollections = {*sequence},
      .miscAssets = {*sequence},
  }};
}

[[nodiscard]] FormatModule wrongTypeCollectionResolverModule() {
  return FormatModule{
      .name = "ProbeWrongTypeRefs",
      .canScan = canScanNever,
      .scan = scanNothing,
      .resolveCollections = wrongTypeCollectionResolver,
  };
}

[[nodiscard]] std::vector<DesiredCollection> duplicateKeyCollectionResolver(const MatchContext&) {
  return {DesiredCollection{
              .key = CollectionKey{.resolver = "ProbeDuplicateKeys", .value = "same-key"},
              .name = "First",
          },
          DesiredCollection{
              .key = CollectionKey{.resolver = "ProbeDuplicateKeys", .value = "same-key"},
              .name = "Second",
          }};
}

[[nodiscard]] FormatModule duplicateKeyCollectionResolverModule() {
  return FormatModule{
      .name = "ProbeDuplicateKeys",
      .canScan = canScanNever,
      .scan = scanNothing,
      .resolveCollections = duplicateKeyCollectionResolver,
  };
}

struct ProbeTrackState {
  u32 program = 0;
};

struct ProbeProgramCommand {
  static constexpr std::string_view kind = "probe.program";
  static constexpr std::string_view name = "Program";
};

struct ProbeNoteCommand {
  static constexpr std::string_view kind = "probe.note";
  static constexpr std::string_view name = "Note";
};

struct ProbeJumpCommand {
  static constexpr CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::AffectsControlFlow;
  static constexpr std::string_view kind = "probe.jump";
  static constexpr std::string_view name = "Jump";
};

struct ProbeDeclaredLoopCommand {
  static constexpr CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::AffectsControlFlow;
  static constexpr std::string_view kind = "probe.declared-loop";
  static constexpr std::string_view name = "Declared Loop";
};

struct ProbeLoopCandidateCommand {
  static constexpr CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::AffectsControlFlow;
  static constexpr std::string_view kind = "probe.loop-candidate";
  static constexpr std::string_view name = "Loop Candidate";
};

struct ProbeCallCommand {
  static constexpr CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::AffectsControlFlow;
  static constexpr std::string_view kind = "probe.call";
  static constexpr std::string_view name = "Call";
};

struct ProbeReturnCommand {
  static constexpr CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::AffectsControlFlow;
  static constexpr std::string_view kind = "probe.return";
  static constexpr std::string_view name = "Return";
};

struct ProbeRepeatCommand {
  static constexpr CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::AffectsControlFlow;
  static constexpr std::string_view kind = "probe.repeat";
  static constexpr std::string_view name = "Repeat";
};

struct ProbeRepeatBreakCommand {
  static constexpr CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::AffectsControlFlow;
  static constexpr std::string_view kind = "probe.repeat-break";
  static constexpr std::string_view name = "Repeat Break";
};

struct ProbeEndCommand {
  static constexpr CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::StopsPlayback;
  static constexpr std::string_view kind = "probe.end";
  static constexpr std::string_view name = "End";
};

struct ProbePlayback {
  ProbeTrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;

  Effects note(u8 key, u32 duration) {
    out.note(static_cast<double>(track.program * 12 + key), 0.5, duration);
    return Effects::wait(duration);
  }

  Effects repeatBreak(u8 slot, Address destination) {
    const BranchResult branch = vm.countedRepeatBreak(slot, destination);
    if (branch.taken) {
      out.instrument(0, 99);
    }
    return branch.effects;
  }
};

using ProbeCompilerCursor = CompilerCursor<ProbeTrackState, ProbePlayback>;

[[nodiscard]] DecodedBytecodeCommand decodeProbeCommand(ByteReader reader, u32 begin) {
  ProbeCompilerCursor cursor(reader, begin, "probe");
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  switch (cursor.opcode()) {
    case 0x80: {
      auto event = cursor.command(ProbeProgramCommand::name, SequenceSemantic::Program, {}, "program");
      event.set<&ProbeTrackState::program>(event.u8("program"));
      return event.emitInstrument(0, event.state<&ProbeTrackState::program>());
    }
    case 0x90: {
      auto event = cursor.command(ProbeNoteCommand::name, SequenceSemantic::Note, {}, "note");
      const u8 key = event.u8("key");
      const u8 duration = event.u8("duration");
      return event.invoke<&ProbePlayback::note>(key, duration);
    }
    case 0xfe: {
      auto event =
          cursor.command(ProbeJumpCommand::name, SequenceSemantic::Jump, ProbeJumpCommand::playbackStatus, "jump");
      return event.jump(event.addressLe("destination", SemanticOperandRole::JumpTarget));
    }
    case 0xfb: {
      auto event = cursor.command(ProbeDeclaredLoopCommand::name, SequenceSemantic::Loop,
                                  ProbeDeclaredLoopCommand::playbackStatus, "declared-loop");
      return event.declaredLoop(event.addressLe("destination", SemanticOperandRole::LoopTarget));
    }
    case 0xfc: {
      auto event = cursor.command(ProbeLoopCandidateCommand::name, SequenceSemantic::Loop,
                                  ProbeLoopCandidateCommand::playbackStatus, "loop-candidate");
      return event.loopCandidate(event.addressLe("destination", SemanticOperandRole::LoopTarget));
    }
    case 0xc0: {
      auto event =
          cursor.command(ProbeCallCommand::name, SequenceSemantic::Call, ProbeCallCommand::playbackStatus, "call");
      return event.call(event.addressLe("destination", SemanticOperandRole::CallTarget));
    }
    case 0xfd:
      return cursor
          .command(ProbeReturnCommand::name, SequenceSemantic::Return, ProbeReturnCommand::playbackStatus, "return")
          .return_();
    case 0xf0: {
      auto event = cursor.command(ProbeRepeatCommand::name, SequenceSemantic::Loop, ProbeRepeatCommand::playbackStatus,
                                  "repeat");
      const u8 slot = event.u8("slot");
      const u8 count = event.u8("count");
      const Address destination = event.addressLe("destination", SemanticOperandRole::RepeatTarget);
      return event.repeatUntil(slot, count, destination);
    }
    case 0xf1: {
      auto event = cursor.command(ProbeRepeatBreakCommand::name, SequenceSemantic::Loop,
                                  ProbeRepeatBreakCommand::playbackStatus, "repeat-break");
      const u8 slot = event.u8("slot");
      const Address destination = event.addressLe("destination", SemanticOperandRole::RepeatTarget);
      return event.invoke<&ProbePlayback::repeatBreak>(slot, destination).mayBranchTo(destination);
    }
    case 0xff:
      return cursor.command(ProbeEndCommand::name, SequenceSemantic::End, ProbeEndCommand::playbackStatus, "end").end();
    default:
      return cursor.unsupported("Unsupported Opcode").stop();
  }
}

[[nodiscard]] SequenceDialect probeSequenceDialect(SequenceProgramBehavior behavior = {}) {
  return makeCompiledDialect<ProbeTrackState, ProbePlayback>(SequenceDialect{
      .id = DialectId{.value = "probe"},
      .commandDetailKindPrefix = "probe",
      .timebase = Timebase{.ppqn = 48},
      .defaultBehavior = behavior,
  });
}

[[nodiscard]] SourceRange probeRange(u64 offset, u64 size) {
  return SourceRange{
      .source = SourceId{0},
      .offset = offset,
      .size = size,
  };
}

template <class Command, size_t Size>
const SourceCommand& addProbeCommand(TrackProgramBuilder& builder, const SequenceDialect& dialect, Address address,
                                     SourceRange range, const std::array<u8, Size>& bytes) {
  static_cast<void>(Command::kind);
  static_cast<void>(dialect);
  const ByteReader reader(range.source, std::span<const u8>{bytes});
  auto decoded = decodeProbeCommand(reader, 0);
  // This helper decodes an isolated command buffer and then places the command
  // at its fixture address. Rebase only its physical continuation; encoded
  // flow destinations already use the fixture's track address space.
  decoded.flow.continuation.value += address.value;
  return builder.addSemantic(address, decoded.opcode, decoded.encodedSize, range, std::move(decoded.operands),
                             std::move(decoded.flow), decoded.annotation, std::move(decoded.execution));
}

[[nodiscard]] size_t countProbeNotesAt(const PerformanceTrack& track, u64 tick) {
  return static_cast<size_t>(std::ranges::count_if(track.events, [tick](const PerformanceEvent& event) {
    const auto* note = std::get_if<NotePerformanceEvent>(&event);
    return note != nullptr && note->header.tick == tick;
  }));
}

[[nodiscard]] const MarkerPerformanceEvent* probeMarkerAt(const PerformanceTrack& track, std::string_view text,
                                                          u64 tick) {
  for (const auto& event : track.events) {
    const auto* marker = std::get_if<MarkerPerformanceEvent>(&event);
    if (marker != nullptr && marker->text == text && marker->header.tick == tick) {
      return marker;
    }
  }
  return nullptr;
}

}  // namespace
