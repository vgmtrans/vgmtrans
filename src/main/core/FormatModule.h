/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "core/Model.h"
#include "core/Source.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

class ScanIdAllocator {
 public:
  [[nodiscard]] AssetId nextAssetId() noexcept;
  [[nodiscard]] CollectionId nextCollectionId() noexcept;
  [[nodiscard]] ItemId nextItemId() noexcept;

  void reserveAfter(AssetId id) noexcept;
  void reserveAfter(CollectionId id) noexcept;
  void reserveAfter(ItemId id) noexcept;

 private:
  u32 nextAssetId_ = 0;
  u32 nextCollectionId_ = 0;
  u32 nextItemId_ = 0;
};

struct ScanInput {
  SourceFile source;
  ByteReader reader;
  ScanIdAllocator& ids;
};

class ItemTreeBuilder {
 public:
  ItemTreeBuilder(ItemTree& tree, ScanIdAllocator& ids);

  [[nodiscard]] ItemId add(
      std::optional<ItemId> parent,
      ItemKind kind,
      std::string detailKind,
      std::string name,
      SourceRange range,
      std::string description = {});

 private:
  ItemTree& tree_;
  ScanIdAllocator& ids_;
};

struct ScanResult {
  std::vector<Asset> assets;
  std::vector<Collection> collections;
  std::vector<Diagnostic> diagnostics;
  std::vector<ExtractedSource> extractedSources;
};

class FormatModule {
 public:
  virtual ~FormatModule() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual bool canScan(const SourceFile& source, std::span<const u8> bytes) const = 0;
  [[nodiscard]] virtual ScanResult scan(const ScanInput& input) const = 0;
};

class FormatRegistry {
 public:
  void add(std::unique_ptr<FormatModule> module);

  [[nodiscard]] const std::vector<std::unique_ptr<FormatModule>>& modules() const noexcept {
    return modules_;
  }

 private:
  std::vector<std::unique_ptr<FormatModule>> modules_;
};

class ScanService {
 public:
  [[nodiscard]] Project scan(SourceStore& sources, const FormatRegistry& formats) const;
};

}  // namespace vgmtrans::core
