/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/ScanValidation.h"

#include "value/validation/SequenceValidation.h"
#include "value/validation/SynthValidation.h"

#include <algorithm>
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
void validateRange(ValidationReport& report, const SourceStore& sources, SourceRange range, std::string_view context,
                   std::string_view resultKind = "Scan result") {
  if (!range.valid()) {
    return;
  }

  if (!sources.contains(range.source)) {
    report.error("scan.range.missing-source",
                 std::string(resultKind) + " contained " + std::string(context) + " range for missing source " +
                     std::to_string(range.source.value),
                 range);
    return;
  }

  const auto sourceSize = sources.source(range.source).size;
  if (range.offset > sourceSize || range.size > sourceSize - range.offset) {
    report.error("scan.range.out-of-bounds",
                 std::string(resultKind) + " contained " + std::string(context) +
                     " range outside source bounds (source " + std::to_string(range.source.value) + ", offset " +
                     std::to_string(range.offset) + ", size " + std::to_string(range.size) + ", source size " +
                     std::to_string(sourceSize) + ")",
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

void validateSoundBankRanges(ValidationReport& report, const SourceStore& sources, const SoundBankAsset& soundBank) {
  for (const auto& instrument : soundBank.instruments) {
    validateRange(report, sources, instrument.range, "instrument");
    for (const auto& region : instrument.regions) {
      validateRange(report, sources, region.range, "instrument region");
    }
  }
  for (const auto& sample : soundBank.localSamples.samples) {
    validateRange(report, sources, sample.encodedData, "local sample encoded data");
  }
}

void validateSamplePoolRanges(ValidationReport& report, const SourceStore& sources, const SamplePoolAsset& samplePool) {
  for (const auto& sample : samplePool.pool.samples) {
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

void validateSourceMapReferences(ValidationReport& report, const SourceMap& sourceMap) {
  for (const auto& annotation : sourceMap.annotations()) {
    if (annotation.parent && (!annotation.parent->valid() || sourceMap.find(*annotation.parent) == nullptr)) {
      report.error("scan.source-annotation.unknown-parent",
                   "Scan result contained source annotation with missing parent annotation id " +
                       std::to_string(annotation.parent->value),
                   annotation.range);
    }

    for (const auto& link : annotation.links) {
      if (const auto* target = std::get_if<SourceAnnotationId>(&link.target);
          target != nullptr && (!target->valid() || sourceMap.find(*target) == nullptr)) {
        report.error(
            "scan.source-annotation.unknown-target",
            "Scan result contained source annotation link to missing annotation id " + std::to_string(target->value),
            annotation.range);
      }
    }
  }
}

void validateSourceMapParentCycles(ValidationReport& report, const SourceMap& sourceMap) {
  // 1 means the annotation is on the current parent path; 2 means its entire
  // parent chain has already been checked.
  std::unordered_map<u32, u8> state;
  state.reserve(sourceMap.annotations().size());
  for (const auto& root : sourceMap.annotations()) {
    if (!root.id.valid() || state[root.id.value] == 2) {
      continue;
    }

    std::vector<u32> path;
    const SourceAnnotation* current = &root;
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
      current = sourceMap.find(*current->parent);
    }
    for (const u32 id : path) {
      state[id] = 2;
    }
  }
}

void validateDiagnosticAnnotationReferences(ValidationReport& report, const std::vector<Diagnostic>& diagnostics,
                                            const SourceMap& sourceMap) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.annotation &&
        (!diagnostic.annotation->valid() || sourceMap.find(*diagnostic.annotation) == nullptr)) {
      report.error("scan.diagnostic.unknown-annotation",
                   "Scan result contained diagnostic for missing source annotation id " +
                       std::to_string(diagnostic.annotation->value),
                   diagnostic.range);
    }
  }
}

void validateAsset(ValidationReport& report, const SourceStore& sources, const Asset& asset,
                   std::span<const SamplePoolAsset* const> samplePools) {
  const auto& meta = metadata(asset);
  validateRange(report, sources, meta.range, "asset metadata");

  // Scan validation owns source-range checks. Domain validators only check the
  // internal structure of the model they receive.
  if (const auto* sequence = std::get_if<SequenceProgramAsset>(&asset)) {
    validateSequenceRanges(report, sources, sequence->program);
    report.merge(validateSequenceProgram(sequence->program));
  } else if (const auto* soundBank = std::get_if<SoundBankAsset>(&asset)) {
    validateSoundBankRanges(report, sources, *soundBank);
    report.merge(validateSoundBank(*soundBank));
    report.merge(validateSampleReferences(*soundBank, samplePools, true));
  } else if (const auto* samplePool = std::get_if<SamplePoolAsset>(&asset)) {
    validateSamplePoolRanges(report, sources, *samplePool);
    report.merge(validateSamplePool(*samplePool));
  }
}

void validateAssetIds(ValidationReport& report, const ScanResult& result,
                      const std::unordered_set<u32>& existingAssetIds, std::unordered_set<u32>& batchAssetIds) {
  // IDs are stable references used by collections, so a scan
  // result must be internally unique and must not reuse IDs already in Session.
  batchAssetIds.reserve(result.assets.size());
  for (const auto& asset : result.assets) {
    const auto id = metadata(asset).id;
    if (!id.valid()) {
      report.error("scan.asset.missing-id", "Scan result contained an asset without an id");
      continue;
    }
    if (!batchAssetIds.insert(id.value).second) {
      report.error("scan.asset.duplicate-id", "Scan result contained duplicate asset id " + std::to_string(id.value));
    }
    if (existingAssetIds.contains(id.value)) {
      report.error("scan.asset.reused-id", "Scan result reused existing asset id " + std::to_string(id.value));
    }
  }
}

void validateSourceMapOwnership(ValidationReport& report, const ScanResult& result,
                                const std::unordered_set<u32>& batchAssetIds) {
  const SourceMap& sourceMap = result.sourceMap;

  std::unordered_map<u32, SourceId> assetSources;
  assetSources.reserve(result.assets.size());
  for (const auto& asset : result.assets) {
    const AssetMetadata& meta = metadata(asset);
    if (!meta.range.valid()) {
      report.error("scan.asset.missing-range", "Scan result contained asset id " + std::to_string(meta.id.value) +
                                                   " without a primary source range");
      continue;
    }
    assetSources.emplace(meta.id.value, meta.range.source);
  }

  std::unordered_set<u32> assetsWithAnnotations;
  assetsWithAnnotations.reserve(result.assets.size());
  for (const auto& annotation : sourceMap.annotations()) {
    const auto inheritedOwner = sourceMap.assetOwner(annotation.id);
    if (annotation.owner && annotation.owner->asset.valid()) {
      const AssetId explicitOwner = annotation.owner->asset;
      if (!batchAssetIds.contains(explicitOwner.value)) {
        report.error("scan.source-annotation.unknown-owner",
                     "Scan result contained source annotation owned by asset id " +
                         std::to_string(explicitOwner.value) + " that was not produced by the scan",
                     annotation.range);
      }
      if (annotation.parent) {
        const auto parentOwner = sourceMap.assetOwner(*annotation.parent);
        if (!parentOwner) {
          report.error("scan.source-annotation.external-asset-parent",
                       "Asset id " + std::to_string(explicitOwner.value) +
                           " has a source annotation whose parent is outside its owned graph",
                       annotation.range);
        } else if (*parentOwner != explicitOwner) {
          report.error("scan.source-annotation.cross-asset-parent",
                       "Source annotation owned by asset id " + std::to_string(explicitOwner.value) +
                           " is nested inside asset id " + std::to_string(parentOwner->value),
                       annotation.range);
        }
      }
    }

    if (!inheritedOwner) {
      continue;
    }
    const auto assetSource = assetSources.find(inheritedOwner->value);
    if (assetSource == assetSources.end()) {
      continue;
    }
    assetsWithAnnotations.insert(inheritedOwner->value);
    if (annotation.range.source != assetSource->second) {
      report.error(
          "scan.asset.multiple-sources",
          "Asset id " + std::to_string(inheritedOwner->value) + " has source annotations in more than one source",
          annotation.range);
    }
  }

  for (const auto& asset : result.assets) {
    const AssetMetadata& meta = metadata(asset);
    if (meta.range.valid() && !assetsWithAnnotations.contains(meta.id.value)) {
      report.error("scan.asset.missing-source-annotations",
                   "Scan result contained asset id " + std::to_string(meta.id.value) +
                       " without an explicitly owned source annotation",
                   meta.range);
    }
  }
}

}  // namespace

ValidationReport validateScanResult(SourceId source, const ScanResult& result, const SourceStore& sources,
                                    const SharedSequence<Asset>& existingAssets) {
  ValidationReport report;

  // This is the boundary between scanner output and durable Session state. Keep
  // all hard admission checks here so format modules do not need their own ID or
  // ownership bookkeeping.
  if (!sources.contains(source)) {
    report.error("scan.source.inactive", "Scan result source is not active");
  }

  std::vector<const SamplePoolAsset*> samplePools;
  for (const auto& asset : result.assets) {
    if (const auto* pool = std::get_if<SamplePoolAsset>(&asset)) {
      samplePools.push_back(pool);
    }
  }
  for (const auto& asset : result.assets) {
    validateAsset(report, sources, asset, samplePools);
    const auto& range = metadata(asset).range;
    if (range.valid() && range.source != source) {
      report.error("scan.asset.foreign-source",
                   "Scan result contained asset id " + std::to_string(metadata(asset).id.value) +
                       " with primary range in source " + std::to_string(range.source.value) +
                       " while scanning source " + std::to_string(source.value),
                   range);
    }
  }

  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.range) {
      validateRange(report, sources, *diagnostic.range, "diagnostic");
    }
  }

  validateSourceMapRanges(report, sources, result.sourceMap);
  validateSourceMapReferences(report, result.sourceMap);
  validateSourceMapParentCycles(report, result.sourceMap);
  validateDiagnosticAnnotationReferences(report, result.diagnostics, result.sourceMap);

  std::unordered_set<u32> existingAssetIds;
  if (!result.assets.empty()) {
    existingAssetIds.reserve(existingAssets.size());
    for (const auto& asset : existingAssets) {
      const AssetId id = metadata(asset).id;
      if (id.valid()) {
        existingAssetIds.insert(id.value);
      }
    }
  }

  std::unordered_set<u32> batchAssetIds;
  validateAssetIds(report, result, existingAssetIds, batchAssetIds);
  validateSourceMapOwnership(report, result, batchAssetIds);

  return report;
}

ValidationReport validateExtractionResult(SourceId source, const ExtractionResult& result, const SourceStore& sources) {
  ValidationReport report;
  if (!sources.contains(source)) {
    report.error("scan.source.inactive", "Extraction result source is not active");
  }

  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.range) {
      validateRange(report, sources, *diagnostic.range, "diagnostic", "Extraction result");
    }
  }

  for (const auto& extracted : result.sources) {
    const auto& origin = extracted.file.origin;
    if (extracted.file.knownFormat && extracted.file.knownFormat->empty()) {
      report.error("scan.extracted-source.empty-known-format",
                   "Extraction result contained a source with an empty known format", origin);
    }
    if (origin && origin->source.valid() && !sources.contains(origin->source)) {
      report.error(
          "scan.extracted-source.missing-parent",
          "Extraction result contained a source with missing parent source " + std::to_string(origin->source.value),
          origin);
    }
    if (origin && sources.contains(origin->source)) {
      validateRange(report, sources, *origin, "extracted source origin", "Extraction result");
    }
  }
  return report;
}

}  // namespace vgmtrans::core
