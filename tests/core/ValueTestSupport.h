/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/synth/DlsExporter.h"
#include "value/export/Export.h"
#include "value/sequence/bytecode/BytecodeSequenceDecoder.h"
#include "value/scan/CollectionResolver.h"
#include "value/scan/FormatModule.h"
#include "value/base/LevelScale.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/ModulationAnalysis.h"
#include "value/sequence/SequenceDialect.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SampleDecoder.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/SoundFontExporter.h"
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
  const auto itemId = input.ids.nextItemId();
  const auto childItemId = input.ids.nextItemId();
  const auto assetRange = input.reader.range(0, input.reader.size());

  SequenceProgramAsset sequence{
      .metadata =
          AssetMetadata{
              .id = assetId,
              .format = "ProbeSequence",
              .name = input.source.name,
              .range = assetRange,
              .items =
                  ItemTree{
                      .root = itemId,
                      .nodes = {ItemNode{
                                    .id = itemId,
                                    .kind = ItemKind::Sequence,
                                    .detailKind = "probe-sequence",
                                    .name = input.source.name,
                                    .range = assetRange,
                                    .children = {ItemId{9999}},
                                },
                                ItemNode{
                                    .id = childItemId,
                                    .parent = itemId,
                                    .kind = ItemKind::Header,
                                    .detailKind = "probe-header",
                                    .name = "Header",
                                    .range = input.reader.range(0, 1),
                                }},
                  },
          },
      .program =
          SequenceProgram{
              .dialect = DialectId{.value = "probe"},
              .timebase = Timebase{.ppqn = 48},
          },
  };

  ScanResult result;
  result.assets.emplace_back(std::move(sequence));
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
  return ScanResult{
      .assets = {MiscAsset{
          .metadata =
              AssetMetadata{
                  .format = "ProbeMisc",
                  .name = input.source.name,
                  .range = input.reader.range(0, input.reader.size()),
              },
          .payload = {input.reader.u8At(0), input.reader.u8At(1)},
      }},
  };
}

[[nodiscard]] FormatModule probeMiscModule() {
  return FormatModule{
      .name = "ProbeMisc",
      .canScan = canScanProbeMisc,
      .scan = scanProbeMisc,
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
  result.matchFacts.push_back(probeBankFact(input, assetId, bank));
  return result;
}

[[nodiscard]] std::vector<DesiredCollection> resolveProbeBankCollections(const MatchContext& context) {
  std::vector<DesiredCollection> collections;
  for (const auto& fact : context.snapshot.matchFacts) {
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

    if (assetById<SequenceProgramAsset>(context.snapshot, fact.asset) != nullptr) {
      found->sequence = fact.asset;
    } else if (assetById<InstrumentSetAsset>(context.snapshot, fact.asset) != nullptr) {
      found->instrumentSets.push_back(fact.asset);
    }

    if (found->sequence && !found->instrumentSets.empty()) {
      found->status = CollectionStatus::Complete;
    }
  }
  for (auto& collection : collections) {
    if (!collection.sequence) {
      collection.status = CollectionStatus::Incomplete;
      collection.issues.push_back(CollectionIssue{
          .severity = Severity::Warning,
          .code = "missing-sequence",
          .message = "Probe bank collection has no sequence",
      });
    }
    if (collection.instrumentSets.empty()) {
      collection.status = CollectionStatus::Incomplete;
      collection.issues.push_back(CollectionIssue{
          .severity = Severity::Warning,
          .code = "missing-instrument-set",
          .message = "Probe bank collection has no instrument set",
      });
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
  ScanResult result;
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
  if (context.snapshot.assets.empty() || context.snapshot.assets.size() > 1) {
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

struct ProbeSequenceContext {
  double linearVelocity = 0.75;
};

struct ProbeTrackState {
  u32 program = 0;
};

struct ProbeProgramCommand {
  u8 program = 0;

  static constexpr std::string_view kind = "probe.program";
  static constexpr std::string_view name = "Program";

  static ProbeProgramCommand parse(CommandReader& in) { return ProbeProgramCommand{.program = in.u8("program")}; }

  Effects execute(ProbeTrackState& state, PerformanceEmitter& out, VmApi&, const ProbeSequenceContext&) const {
    state.program = program;
    out.instrument(InstrumentPerformanceEvent{
        .program = program,
    });
    return Effects::none();
  }
};

struct ProbeNoteCommand {
  u8 key = 0;
  u8 duration = 0;

  static constexpr std::string_view kind = "probe.note";
  static constexpr std::string_view name = "Note";

  static ProbeNoteCommand parse(CommandReader& in) {
    return ProbeNoteCommand{
        .key = in.u8("key"),
        .duration = in.u8("duration"),
    };
  }

  Effects execute(ProbeTrackState& state, PerformanceEmitter& out, VmApi&, const ProbeSequenceContext& context) const {
    // This mirrors a source driver using the current track program as a key bank.
    out.note(NotePerformanceEvent{
        .key = static_cast<double>(state.program * 12 + key),
        .linearVelocity = context.linearVelocity,
        .durationTicks = duration,
    });
    return Effects::wait(duration);
  }
};

struct ProbeJumpCommand {
  Address destination;

  static constexpr std::string_view kind = "probe.jump";
  static constexpr std::string_view name = "Jump";

  static ProbeJumpCommand parse(CommandReader& in) {
    return ProbeJumpCommand{.destination = in.le16Address("destination")};
  }

  Effects execute(ProbeTrackState&, PerformanceEmitter&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.jump(destination)};
  }
};

struct ProbeLoopForeverCommand {
  Address destination;

  static constexpr std::string_view kind = "probe.loop-forever";
  static constexpr std::string_view name = "Loop Forever";

  static ProbeLoopForeverCommand parse(CommandReader& in) {
    return ProbeLoopForeverCommand{.destination = in.le16Address("destination")};
  }

  Effects execute(ProbeTrackState&, PerformanceEmitter&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.loopForever(destination)};
  }
};

struct ProbeJumpOrLoopForeverCommand {
  Address destination;

  static constexpr std::string_view kind = "probe.jump-or-loop-forever";
  static constexpr std::string_view name = "Jump Or Loop Forever";

  static ProbeJumpOrLoopForeverCommand parse(CommandReader& in) {
    return ProbeJumpOrLoopForeverCommand{.destination = in.le16Address("destination")};
  }

  Effects execute(ProbeTrackState&, PerformanceEmitter&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.jumpOrLoopForever(destination)};
  }
};

struct ProbeCallCommand {
  Address destination;

  static constexpr std::string_view kind = "probe.call";
  static constexpr std::string_view name = "Call";

  static ProbeCallCommand parse(CommandReader& in) {
    return ProbeCallCommand{.destination = in.le16Address("destination")};
  }

  Effects execute(ProbeTrackState&, PerformanceEmitter&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.call(destination)};
  }
};

struct ProbeReturnCommand {
  static constexpr std::string_view kind = "probe.return";
  static constexpr std::string_view name = "Return";

  static ProbeReturnCommand parse(CommandReader&) { return ProbeReturnCommand{}; }

  Effects execute(ProbeTrackState&, PerformanceEmitter&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.return_()};
  }
};

struct ProbeRepeatCommand {
  u8 slot = 0;
  u8 count = 0;
  Address destination;

  static constexpr std::string_view kind = "probe.repeat";
  static constexpr std::string_view name = "Repeat";

  static ProbeRepeatCommand parse(CommandReader& in) {
    return ProbeRepeatCommand{
        .slot = in.u8("slot"),
        .count = in.u8("count"),
        .destination = in.le16Address("destination"),
    };
  }

  Effects execute(ProbeTrackState&, PerformanceEmitter&, VmApi& vm, const ProbeSequenceContext&) const {
    return vm.repeatUntilEffect(slot, count, destination);
  }
};

struct ProbeRepeatBreakCommand {
  u8 slot = 0;
  Address destination;

  static constexpr std::string_view kind = "probe.repeat-break";
  static constexpr std::string_view name = "Repeat Break";

  static ProbeRepeatBreakCommand parse(CommandReader& in) {
    return ProbeRepeatBreakCommand{
        .slot = in.u8("slot"),
        .destination = in.le16Address("destination"),
    };
  }

  Effects execute(ProbeTrackState&, PerformanceEmitter& out, VmApi& vm, const ProbeSequenceContext&) const {
    const BranchResult branch = vm.repeatBreakBranch(slot, destination);
    if (branch.taken) {
      out.instrument(InstrumentPerformanceEvent{.program = 99});
    }
    return branch.effects;
  }
};

struct ProbeEndCommand {
  static constexpr std::string_view kind = "probe.end";
  static constexpr std::string_view name = "End";

  static ProbeEndCommand parse(CommandReader&) { return ProbeEndCommand{}; }

  Effects execute(ProbeTrackState&, PerformanceEmitter&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.end()};
  }
};

[[nodiscard]] SequenceDialect probeSequenceDialect(SequenceProgramBehavior behavior = {}) {
  return SequenceDialectBuilder<ProbeTrackState, ProbeSequenceContext>("probe",
                                                                       ProbeSequenceContext{.linearVelocity = 0.5})
      .timebase(Timebase{.ppqn = 48})
      .defaultBehavior(behavior)
      .commands<ProbeProgramCommand, ProbeNoteCommand, ProbeJumpCommand, ProbeLoopForeverCommand,
                ProbeJumpOrLoopForeverCommand, ProbeCallCommand, ProbeReturnCommand, ProbeRepeatCommand,
                ProbeRepeatBreakCommand, ProbeEndCommand>();
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
  const auto* handler = dialect.handlerForKind(Command::kind);
  if (handler == nullptr) {
    throw std::runtime_error("probe command handler was not registered");
  }
  return builder.add<Command>(handler->id, handler->kind, address, range, std::span<const u8>{bytes});
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
