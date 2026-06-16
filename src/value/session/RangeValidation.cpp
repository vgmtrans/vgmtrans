/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/session/RangeValidation.h"

#include "value/session/ScanCommit.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::string rangeContext(std::string_view context) {
  return std::string(context);
}

void validateRange(const SourceStore& sources, SourceRange range, std::string_view context) {
  if (!range.valid()) {
    return;
  }

  if (!sources.contains(range.source)) {
    throw std::invalid_argument("Scan result contained " + rangeContext(context) + " range for missing source " +
                                std::to_string(range.source.value));
  }

  const auto sourceSize = sources.source(range.source).size;
  if (range.offset > sourceSize || range.size > sourceSize - range.offset) {
    throw std::invalid_argument("Scan result contained " + rangeContext(context) +
                                " range outside source bounds (source " + std::to_string(range.source.value) +
                                ", offset " + std::to_string(range.offset) + ", size " + std::to_string(range.size) +
                                ", source size " + std::to_string(sourceSize) + ")");
  }
}

void validateItemTreeRanges(const SourceStore& sources, const ItemTree& items) {
  for (const auto& item : items.nodes) {
    validateRange(sources, item.range, "asset item");
  }
}

void validateSequenceRanges(const SourceStore& sources, const SequenceProgram& program) {
  for (const auto& track : program.tracks) {
    for (const auto& command : track.commands) {
      validateRange(sources, command.range, "sequence command");
    }
    for (const auto& operand : track.operands) {
      validateRange(sources, operand.range, "sequence command operand");
    }
  }

  for (const auto& instrument : program.referencedInstruments) {
    if (instrument.range) {
      validateRange(sources, *instrument.range, "sequence instrument reference");
    }
  }
}

void validateInstrumentSetRanges(const SourceStore& sources, const InstrumentSetAsset& instrumentSet) {
  for (const auto& instrument : instrumentSet.instruments) {
    validateRange(sources, instrument.range, "instrument");
    for (const auto& region : instrument.regions) {
      validateRange(sources, region.range, "instrument region");
    }
  }
}

void validateSampleCollectionRanges(const SourceStore& sources, const SampleCollectionAsset& sampleCollection) {
  for (const auto& sample : sampleCollection.samples.samples) {
    validateRange(sources, sample.encodedData, "sample encoded data");
  }
}

void validateAssetRanges(const SourceStore& sources, const Asset& asset) {
  const auto& meta = metadata(asset);
  validateRange(sources, meta.range, "asset metadata");
  validateItemTreeRanges(sources, meta.items);

  if (const auto* sequence = std::get_if<SequenceProgramAsset>(&asset)) {
    validateSequenceRanges(sources, sequence->program);
  } else if (const auto* instrumentSet = std::get_if<InstrumentSetAsset>(&asset)) {
    validateInstrumentSetRanges(sources, *instrumentSet);
  } else if (const auto* sampleCollection = std::get_if<SampleCollectionAsset>(&asset)) {
    validateSampleCollectionRanges(sources, *sampleCollection);
  }
}

}  // namespace

void validateScanCommitRanges(const ScanCommit& commit, const SourceStore& sources) {
  for (const auto& asset : commit.assets) {
    validateAssetRanges(sources, asset);
  }

  for (const auto& diagnostic : commit.diagnostics) {
    if (diagnostic.range) {
      validateRange(sources, *diagnostic.range, "diagnostic");
    }
  }

  for (const auto& extracted : commit.extractedSources) {
    if (extracted.origin && sources.contains(extracted.origin->source)) {
      validateRange(sources, *extracted.origin, "extracted source origin");
    }
  }
}

}  // namespace vgmtrans::core
