/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/CollectionPreparation.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

CollectionPreparation::CollectionPreparation(const MaterializationContext& context)
    : context_(context), result_{.collection = context.collection},
      // Prepared annotations enter the same session-wide source map as scan
      // annotations, so their IDs must come from the shared allocator too.
      sourceMap_([&context]() { return context.ids.nextSourceAnnotationId(); }) {
}

InstrumentSetBuilder CollectionPreparation::instruments(std::string_view slot) {
  return InstrumentSetBuilder{assetIdForSlot(slot), &sourceMap_, &result_.diagnostics};
}

SampleCollectionBuilder CollectionPreparation::samples(std::string_view slot) {
  return SampleCollectionBuilder{assetIdForSlot(slot), &sourceMap_, &result_.diagnostics};
}

void CollectionPreparation::keepInstrumentSet(AssetId asset) {
  addUnique(result_.collection.instrumentSets, asset);
}

void CollectionPreparation::keepSampleCollection(AssetId asset) {
  addUnique(result_.collection.sampleCollections, asset);
}

void CollectionPreparation::keepMisc(AssetId asset) {
  addUnique(result_.collection.miscAssets, asset);
}

void CollectionPreparation::removeInstrumentSet(AssetId asset) {
  remove(result_.collection.instrumentSets, asset);
}

void CollectionPreparation::removeSampleCollection(AssetId asset) {
  remove(result_.collection.sampleCollections, asset);
}

void CollectionPreparation::removeMisc(AssetId asset) {
  remove(result_.collection.miscAssets, asset);
}

void CollectionPreparation::appendInstrumentSet(std::string name, InstrumentSetBuilder&& instruments) {
  const AssetId id = instruments.assetId();
  auto values = std::move(instruments).finish();
  addAsset(takeSlot(id), InstrumentSetAsset{
                             .metadata =
                                 AssetMetadata{
                                     .id = id,
                                     .format = result_.collection.key.resolver,
                                     .name = std::move(name),
                                     .range = instruments.range(),
                                 },
                             .instruments = std::move(values),
                         });
  addUnique(result_.collection.instrumentSets, id);
}

void CollectionPreparation::appendSampleCollection(std::string name, SampleCollectionBuilder&& samples) {
  const AssetId id = samples.assetId();
  auto values = std::move(samples).finish();
  addAsset(takeSlot(id), SampleCollectionAsset{
                             .metadata =
                                 AssetMetadata{
                                     .id = id,
                                     .format = result_.collection.key.resolver,
                                     .name = std::move(name),
                                     .range = samples.range(),
                                 },
                             .samples = std::move(values),
                         });
  addUnique(result_.collection.sampleCollections, id);
}

void CollectionPreparation::replaceInstrumentSet(std::string name, InstrumentSetBuilder&& instruments) {
  result_.collection.instrumentSets.clear();
  appendInstrumentSet(std::move(name), std::move(instruments));
}

void CollectionPreparation::replaceSampleCollection(std::string name, SampleCollectionBuilder&& samples) {
  result_.collection.sampleCollections.clear();
  appendSampleCollection(std::move(name), std::move(samples));
}

MaterializationResult CollectionPreparation::incomplete(std::string message, std::optional<SourceRange> range) {
  // A failed preparation returns the immutable base collection plus one clear
  // issue. Partial derived assets would leave references and UI annotations
  // whose meaning depends on how far construction happened to get.
  result_.collection = context_.collection;
  result_.assets.clear();
  result_.diagnostics.clear();
  pendingSlots_.clear();
  discardSourceMap_ = true;
  result_.collection.status = CollectionStatus::Incomplete;
  result_.collection.issues.push_back(CollectionIssue{
      .severity = Severity::Warning,
      .code = "collection-preparation-incomplete",
      .message = message,
      .range = range,
  });
  result_.diagnostics.push_back(Diagnostic{
      .severity = Severity::Warning,
      .code = "collection-preparation-incomplete",
      .message = std::move(message),
      .range = range,
  });
  return std::move(*this).finish();
}

MaterializationResult CollectionPreparation::finish() && {
  if (finished_) {
    throw std::logic_error("CollectionPreparation was finished more than once");
  }
  if (!pendingSlots_.empty()) {
    throw std::logic_error("CollectionPreparation contains a builder that was never committed");
  }
  auto sourceMap = sourceMap_.finish();
  if (!discardSourceMap_) {
    result_.sourceMap = std::move(sourceMap);
  }
  finished_ = true;
  return std::move(result_);
}

AssetId CollectionPreparation::assetIdForSlot(std::string_view slot) {
  if (finished_) {
    throw std::logic_error("Cannot create an asset after CollectionPreparation::finish()");
  }
  if (slot.empty()) {
    throw std::invalid_argument("CollectionPreparation asset slot must not be empty");
  }
  if (!context_.assetIdForSlot) {
    throw std::logic_error("CollectionPreparation requires a stable asset-slot allocator");
  }
  // Named slots let the session reuse a derived asset's identity when the same
  // collection is rebuilt, which keeps UI selections and source owners stable.
  const AssetId id = context_.assetIdForSlot(slot);
  const auto [found, inserted] = pendingSlots_.try_emplace(id.value, slot);
  if (!inserted && found->second != slot) {
    throw std::logic_error("CollectionPreparation slot allocator returned one id for different slots");
  }
  return id;
}

std::string CollectionPreparation::takeSlot(AssetId asset) {
  const auto found = pendingSlots_.find(asset.value);
  if (found == pendingSlots_.end()) {
    throw std::logic_error("CollectionPreparation can commit only builders created by this preparation");
  }
  std::string slot = std::move(found->second);
  pendingSlots_.erase(found);
  return slot;
}

void CollectionPreparation::addAsset(std::string slot, Asset asset) {
  const auto duplicate = std::ranges::find(result_.assets, slot, &MaterializedAsset::slot);
  if (duplicate != result_.assets.end()) {
    throw std::logic_error("CollectionPreparation committed materialization slot more than once");
  }
  result_.assets.push_back(MaterializedAsset{.slot = std::move(slot), .asset = std::move(asset)});
}

void CollectionPreparation::addUnique(std::vector<AssetId>& assets, AssetId asset) {
  if (std::ranges::find(assets, asset) == assets.end()) {
    assets.push_back(asset);
  }
}

void CollectionPreparation::remove(std::vector<AssetId>& assets, AssetId asset) {
  std::erase(assets, asset);
}

}  // namespace vgmtrans::core
