/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/FormatModule.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace vgmtrans::core {

namespace {

ItemNode* findItem(ItemTree& items, ItemId id) {
  const auto found = std::ranges::find_if(items.nodes, [id](const ItemNode& item) {
    return item.id == id;
  });
  if (found == items.nodes.end()) {
    return nullptr;
  }
  return &*found;
}

void normalizeItemTree(ItemTree& items, ScanIdAllocator& ids) {
  for (auto& item : items.nodes) {
    if (item.id.valid()) {
      ids.reserveAfter(item.id);
    } else {
      item.id = ids.nextItemId();
    }
    item.children.clear();
  }

  if (items.nodes.empty()) {
    items.root = std::nullopt;
    return;
  }

  std::optional<ItemId> firstRoot;
  for (auto& item : items.nodes) {
    if (!item.parent.has_value()) {
      if (!firstRoot.has_value()) {
        firstRoot = item.id;
      }
      continue;
    }

    if (auto* parent = findItem(items, *item.parent)) {
      parent->children.push_back(item.id);
    } else {
      item.parent = std::nullopt;
      if (!firstRoot.has_value()) {
        firstRoot = item.id;
      }
    }
  }

  if (!items.root.has_value() || !findItem(items, *items.root)) {
    items.root = firstRoot;
  }
}

void assignMissingAssetIds(std::vector<Asset>& assets, ScanIdAllocator& ids) {
  for (auto& asset : assets) {
    auto& meta = metadata(asset);
    if (meta.id.valid()) {
      ids.reserveAfter(meta.id);
    } else {
      meta.id = ids.nextAssetId();
    }
    normalizeItemTree(meta.items, ids);
  }
}

void assignMissingCollectionIds(std::vector<Collection>& collections, ScanIdAllocator& ids) {
  for (auto& collection : collections) {
    if (collection.id.valid()) {
      ids.reserveAfter(collection.id);
    } else {
      collection.id = ids.nextCollectionId();
    }
  }
}

Diagnostic errorDiagnostic(std::string message, std::optional<SourceRange> range = std::nullopt) {
  return Diagnostic{.severity = Severity::Error, .message = std::move(message), .range = range};
}

}  // namespace

AssetId ScanIdAllocator::nextAssetId() noexcept {
  return AssetId{nextAssetId_++};
}

CollectionId ScanIdAllocator::nextCollectionId() noexcept {
  return CollectionId{nextCollectionId_++};
}

ItemId ScanIdAllocator::nextItemId() noexcept {
  return ItemId{nextItemId_++};
}

void ScanIdAllocator::reserveAfter(AssetId id) noexcept {
  if (id.valid()) {
    nextAssetId_ = std::max(nextAssetId_, id.value + 1);
  }
}

void ScanIdAllocator::reserveAfter(CollectionId id) noexcept {
  if (id.valid()) {
    nextCollectionId_ = std::max(nextCollectionId_, id.value + 1);
  }
}

void ScanIdAllocator::reserveAfter(ItemId id) noexcept {
  if (id.valid()) {
    nextItemId_ = std::max(nextItemId_, id.value + 1);
  }
}

void FormatRegistry::add(std::unique_ptr<FormatModule> module) {
  if (!module) {
    throw std::invalid_argument("Cannot register a null FormatModule");
  }
  modules_.push_back(std::move(module));
}

Project ScanService::scan(SourceStore& sources, const FormatRegistry& formats) const {
  sources.discardVirtualizedTail();

  Project project;
  ScanIdAllocator ids;

  for (size_t sourceIndex = 0; sourceIndex < sources.sourceCount(); ++sourceIndex) {
    const auto source = sources.sourceAt(sourceIndex);
    const auto bytes = sources.bytes(source.id);

    for (const auto& module : formats.modules()) {
      bool shouldScan = false;
      try {
        shouldScan = module->canScan(source, bytes);
      } catch (const std::exception& ex) {
        project.diagnostics.push_back(
            errorDiagnostic(std::string(module->name()) + " canScan failed: " + ex.what()));
      }

      if (!shouldScan) {
        continue;
      }

      try {
        ScanResult result = module->scan(ScanInput{
            .source = source,
            .reader = sources.reader(source.id),
            .ids = ids,
        });

        assignMissingAssetIds(result.assets, ids);
        assignMissingCollectionIds(result.collections, ids);

        project.assets.insert(project.assets.end(),
                              std::make_move_iterator(result.assets.begin()),
                              std::make_move_iterator(result.assets.end()));
        project.collections.insert(project.collections.end(),
                                   std::make_move_iterator(result.collections.begin()),
                                   std::make_move_iterator(result.collections.end()));
        project.diagnostics.insert(project.diagnostics.end(),
                                   std::make_move_iterator(result.diagnostics.begin()),
                                   std::make_move_iterator(result.diagnostics.end()));

        for (auto& extracted : result.extractedSources) {
          extracted.file.virtualized = true;
          extracted.file.origin = extracted.origin;
          sources.add(std::move(extracted.file), std::move(extracted.bytes));
        }
      } catch (const std::exception& ex) {
        project.diagnostics.push_back(
            errorDiagnostic(std::string(module->name()) + " scan failed: " + ex.what(),
                            SourceRange{.source = source.id, .offset = 0, .size = source.size}));
      }
    }
  }

  project.sources = sources.sourceFiles();
  return project;
}

}  // namespace vgmtrans::core
