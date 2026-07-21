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
#include <unordered_map>
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

void validateSequenceRanges(ValidationReport& report, const SourceStore& sources, const SequenceProgram& program) {
  for (const auto& track : program.tracks) {
    for (const auto& command : track.commands) {
      validateRange(report, sources, command.range, "sequence command");
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
    if (!annotation.range.valid()) {
      report.error("scan.source-annotation.missing-range",
                   "Scan result contained source annotation without a primary source range");
    } else {
      validateRange(report, sources, annotation.range, "source annotation");
    }
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

[[nodiscard]] std::unordered_set<u32> sourceAnnotationIds(const SourceMap& sourceMap) {
  std::unordered_set<u32> ids;
  for (const auto& annotation : sourceMap.annotations()) {
    if (annotation.id.valid()) {
      ids.insert(annotation.id.value);
    }
  }
  return ids;
}

[[nodiscard]] bool containsAnnotationId(const std::unordered_set<u32>& ids, SourceAnnotationId id) {
  return id.valid() && ids.contains(id.value);
}

void validateSourceMapReferences(ValidationReport& report, const SourceMap& sourceMap) {
  const auto annotationIds = sourceAnnotationIds(sourceMap);
  for (const auto& annotation : sourceMap.annotations()) {
    if (annotation.parent && !containsAnnotationId(annotationIds, *annotation.parent)) {
      report.error("scan.source-annotation.unknown-parent",
                   "Scan result contained source annotation with missing parent annotation id " +
                       std::to_string(annotation.parent->value),
                   annotation.range);
    }

    for (const auto& link : annotation.links) {
      if (const auto* target = std::get_if<SourceAnnotationId>(&link.target);
          target != nullptr && !containsAnnotationId(annotationIds, *target)) {
        report.error(
            "scan.source-annotation.unknown-target",
            "Scan result contained source annotation link to missing annotation id " + std::to_string(target->value),
            annotation.range);
      }
    }
  }
}

void validateSourceMapParentCycles(ValidationReport& report, const SourceMap& sourceMap) {
  std::unordered_map<u32, const SourceAnnotation*> annotations;
  annotations.reserve(sourceMap.annotations().size());
  for (const auto& annotation : sourceMap.annotations()) {
    if (annotation.id.valid()) {
      annotations.emplace(annotation.id.value, &annotation);
    }
  }

  // 1 means the annotation is on the current parent path; 2 means its entire
  // parent chain has already been checked.
  std::unordered_map<u32, u8> state;
  state.reserve(annotations.size());
  for (const auto& [rootId, root] : annotations) {
    if (state[rootId] == 2) {
      continue;
    }

    std::vector<u32> path;
    const SourceAnnotation* current = root;
    while (current != nullptr) {
      const u32 id = current->id.value;
      if (state[id] == 1) {
        report.error("scan.source-annotation.parent-cycle",
                     "Scan result contained a cycle in source annotation parents", current->range);
        break;
      }
      if (state[id] == 2) {
        break;
      }
      state[id] = 1;
      path.push_back(id);
      if (!current->parent) {
        break;
      }
      const auto parent = annotations.find(current->parent->value);
      current = parent != annotations.end() ? parent->second : nullptr;
    }
    for (const u32 id : path) {
      state[id] = 2;
    }
  }
}

void validateDiagnosticAnnotationReferences(ValidationReport& report, const std::vector<Diagnostic>& diagnostics,
                                            const SourceMap& sourceMap) {
  const auto annotationIds = sourceAnnotationIds(sourceMap);
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.annotation && !containsAnnotationId(annotationIds, *diagnostic.annotation)) {
      report.error("scan.diagnostic.unknown-annotation",
                   "Scan result contained diagnostic for missing source annotation id " +
                       std::to_string(diagnostic.annotation->value),
                   diagnostic.range);
    }
  }
}

void validateAsset(ValidationReport& report, const SourceStore& sources, const Asset& asset) {
  const auto& meta = metadata(asset);
  validateRange(report, sources, meta.range, "asset metadata");

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
  validateSourceMapReferences(report, commit.sourceMap);
  validateSourceMapParentCycles(report, commit.sourceMap);
  validateDiagnosticAnnotationReferences(report, commit.diagnostics, commit.sourceMap);

  std::unordered_set<u32> batchAssetIds;
  validateAssetIds(report, commit, existingAssets, batchAssetIds);
  validateMatchFacts(report, commit, sources, existingAssets, batchAssetIds);

  return report;
}

}  // namespace vgmtrans::core
