/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"
#include "value/synth/SynthBuilder.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vgmtrans::core {

// Assembles collection-specific derived assets after matching has selected all
// dependencies. Static formats never need this class; exceptional formats reuse
// the ordinary synth builders instead of gaining a second construction model.
class CollectionPreparation {
public:
  explicit CollectionPreparation(const MaterializationContext& context);

  CollectionPreparation(const CollectionPreparation&) = delete;
  CollectionPreparation& operator=(const CollectionPreparation&) = delete;

  [[nodiscard]] const MaterializationContext& context() const noexcept { return context_; }
  [[nodiscard]] const SessionSnapshot& snapshot() const noexcept { return context_.snapshot; }
  [[nodiscard]] const DesiredCollection& baseCollection() const noexcept { return context_.collection; }
  [[nodiscard]] const DesiredCollection& collection() const noexcept { return result_.collection; }

  [[nodiscard]] InstrumentSetBuilder instruments(std::string_view slot);
  [[nodiscard]] SampleCollectionBuilder samples(std::string_view slot);

  void keepInstrumentSet(AssetId asset);
  void keepSampleCollection(AssetId asset);
  void keepMisc(AssetId asset);
  void removeInstrumentSet(AssetId asset);
  void removeSampleCollection(AssetId asset);
  void removeMisc(AssetId asset);

  void appendInstrumentSet(std::string name, InstrumentSetBuilder&& instruments);
  void appendSampleCollection(std::string name, SampleCollectionBuilder&& samples);
  void replaceInstrumentSet(std::string name, InstrumentSetBuilder&& instruments);
  void replaceSampleCollection(std::string name, SampleCollectionBuilder&& samples);

  [[nodiscard]] MaterializationResult incomplete(std::string message, std::optional<SourceRange> range = std::nullopt);
  [[nodiscard]] MaterializationResult finish() &&;

private:
  [[nodiscard]] AssetId assetIdForSlot(std::string_view slot);
  [[nodiscard]] std::string takeSlot(AssetId asset);
  void addAsset(std::string slot, Asset asset);
  static void addUnique(std::vector<AssetId>& assets, AssetId asset);
  static void remove(std::vector<AssetId>& assets, AssetId asset);

  const MaterializationContext& context_;
  MaterializationResult result_;
  SourceMapBuilder sourceMap_;
  std::unordered_map<u32, std::string> pendingSlots_;
  bool discardSourceMap_ = false;
  bool finished_ = false;
};

}  // namespace vgmtrans::core
