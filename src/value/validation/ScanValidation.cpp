/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/ScanValidation.h"

#include "value/session/AssetStore.h"
#include "value/session/ScanCommit.h"
#include "value/validation/SequenceValidation.h"
#include "value/validation/SynthValidation.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace vgmtrans::core {

namespace {

// SourceRange is the shared link from parsed values back to SourceStore bytes.
// Invalid ranges are allowed when a value genuinely has no source bytes, but a
// valid range must point at active bytes that fit in the source.
void validateRange(ValidationReport& report, const SourceStore& sources, SourceRange range, std::string_view context) {
  if (!range.valid()) {
    return;
  }

  if (!sources.contains(range.source)) {
    report.error("scan.range.missing-source",
                 "Scan result contained " + std::string(context) + " range for missing source " +
                     std::to_string(range.source.value),
                 range);
    return;
  }

  const auto sourceSize = sources.source(range.source).size;
  if (range.offset > sourceSize || range.size > sourceSize - range.offset) {
    report.error("scan.range.out-of-bounds",
                 "Scan result contained " + std::string(context) + " range outside source bounds (source " +
                     std::to_string(range.source.value) + ", offset " + std::to_string(range.offset) + ", size " +
                     std::to_string(range.size) + ", source size " + std::to_string(sourceSize) + ")",
                 range);
  }
}

void validateItemTreeRanges(ValidationReport& report, const SourceStore& sources, const ItemTree& items) {
  for (const auto& item : items.nodes) {
    validateRange(report, sources, item.range, "asset item");
  }
}

void validateSequenceRanges(ValidationReport& report, const SourceStore& sources, const SequenceProgram& program) {
  for (const auto& track : program.tracks) {
    for (const auto& command : track.commands) {
      validateRange(report, sources, command.range, "sequence command");
    }
    for (const auto& operand : track.operands) {
      validateRange(report, sources, operand.range, "sequence command operand");
    }
  }

  for (const auto& instrument : program.referencedInstruments) {
    if (instrument.range) {
      validateRange(report, sources, *instrument.range, "sequence instrument reference");
    }
  }
}

void validateInstrumentSetRanges(ValidationReport& report, const SourceStore& sources,
                                 const InstrumentSetAsset& instrumentSet) {
  for (const auto& instrument : instrumentSet.instruments) {
    validateRange(report, sources, instrument.range, "instrument");
    for (const auto& region : instrument.regions) {
      validateRange(report, sources, region.range, "instrument region");
    }
  }
}

void validateSampleCollectionRanges(ValidationReport& report, const SourceStore& sources,
                                    const SampleCollectionAsset& sampleCollection) {
  for (const auto& sample : sampleCollection.samples.samples) {
    validateRange(report, sources, sample.encodedData, "sample encoded data");
  }
}

void validateSourceMapRanges(ValidationReport& report, const SourceStore& sources, const SourceMap& sourceMap) {
  for (const auto& annotation : sourceMap.annotations()) {
    validateRange(report, sources, annotation.range, "source annotation");
    for (const auto& field : annotation.fields) {
      validateRange(report, sources, field.range, "source annotation field");
    }
    for (const auto& link : annotation.links) {
      if (const auto* range = std::get_if<SourceRange>(&link.target)) {
        validateRange(report, sources, *range, "source annotation link");
      }
    }
  }
}

void validateAsset(ValidationReport& report, const SourceStore& sources, const Asset& asset) {
  const auto& meta = metadata(asset);
  validateRange(report, sources, meta.range, "asset metadata");
  validateItemTreeRanges(report, sources, meta.items);

  // Scan validation owns source-range checks. Domain validators only check the
  // internal structure of the model they receive.
  if (const auto* sequence = std::get_if<SequenceProgramAsset>(&asset)) {
    validateSequenceRanges(report, sources, sequence->program);
    report.merge(validateSequenceProgram(sequence->program));
  } else if (const auto* instrumentSet = std::get_if<InstrumentSetAsset>(&asset)) {
    validateInstrumentSetRanges(report, sources, *instrumentSet);
    report.merge(validateInstrumentSet(*instrumentSet));
  } else if (const auto* sampleCollection = std::get_if<SampleCollectionAsset>(&asset)) {
    validateSampleCollectionRanges(report, sources, *sampleCollection);
    report.merge(validateSampleCollection(*sampleCollection));
  }
}

void validateAssetIds(ValidationReport& report, const ScanCommit& commit, const AssetStore& existingAssets,
                      std::unordered_set<u32>& batchAssetIds) {
  // IDs are stable references used by match facts and collections, so a scan
  // result must be internally unique and must not reuse IDs already in Session.
  batchAssetIds.reserve(commit.assets.size());
  for (const auto& asset : commit.assets) {
    const auto id = metadata(asset).id;
    if (!id.valid()) {
      report.error("scan.asset.missing-id", "Scan result contained an asset without an id");
      continue;
    }
    if (!batchAssetIds.insert(id.value).second) {
      report.error("scan.asset.duplicate-id", "Scan result contained duplicate asset id " + std::to_string(id.value));
    }
    if (existingAssets.contains(id)) {
      report.error("scan.asset.reused-id", "Scan result reused existing asset id " + std::to_string(id.value));
    }
  }
}

void validateMatchFacts(ValidationReport& report, const ScanCommit& commit, const SourceStore& sources,
                        const AssetStore& existingAssets, const std::unordered_set<u32>& batchAssetIds) {
  // Match facts may point at assets from this scan or assets already accepted
  // from earlier source loads. They must never point at a missing source.
  for (const auto& fact : commit.matchFacts) {
    if (!fact.asset.valid()) {
      report.error("scan.match-fact.missing-asset", "Scan result contained a match fact without an asset id");
    } else if (!batchAssetIds.contains(fact.asset.value) && !existingAssets.contains(fact.asset)) {
      report.error("scan.match-fact.unknown-asset",
                   "Scan result contained a match fact for missing asset id " + std::to_string(fact.asset.value));
    }

    if (fact.scope.kind == MatchScopeKind::Source && !fact.scope.source) {
      report.error("scan.match-fact.missing-source-scope",
                   "Scan result contained a source-scoped match fact without a source id");
    }
    if (fact.scope.source && !sources.contains(*fact.scope.source)) {
      report.error("scan.match-fact.unknown-source", "Scan result contained a match fact for missing source id " +
                                                         std::to_string(fact.scope.source->value));
    }
  }
}

void validateExtractedSources(ValidationReport& report, const ScanCommit& commit, const SourceStore& sources) {
  for (const auto& extracted : commit.extractedSources) {
    if (extracted.origin && extracted.origin->source.valid() && !sources.contains(extracted.origin->source)) {
      report.error("scan.extracted-source.missing-parent",
                   "Scan result contained extracted source with missing parent source " +
                       std::to_string(extracted.origin->source.value),
                   extracted.origin);
    }

    if (extracted.origin && sources.contains(extracted.origin->source)) {
      validateRange(report, sources, *extracted.origin, "extracted source origin");
    }
  }
}

}  // namespace

ValidationReport validateScanCommit(const ScanCommit& commit, const SourceStore& sources,
                                    const AssetStore& existingAssets) {
  ValidationReport report;

  // This is the boundary between scanner output and durable Session state. Keep
  // all hard admission checks here so format modules do not need their own ID or
  // ownership bookkeeping.
  if (!sources.contains(commit.source)) {
    report.error("scan.source.inactive", "Scan result source is not active");
  }

  for (const auto& asset : commit.assets) {
    validateAsset(report, sources, asset);
  }

  for (const auto& diagnostic : commit.diagnostics) {
    if (diagnostic.range) {
      validateRange(report, sources, *diagnostic.range, "diagnostic");
    }
  }

  validateExtractedSources(report, commit, sources);
  validateSourceMapRanges(report, sources, commit.sourceMap);

  std::unordered_set<u32> batchAssetIds;
  validateAssetIds(report, commit, existingAssets, batchAssetIds);
  validateMatchFacts(report, commit, sources, existingAssets, batchAssetIds);

  return report;
}

}  // namespace vgmtrans::core
