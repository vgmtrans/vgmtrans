/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/Scan.h"

#include "value/core/FormatModule.h"
#include "value/core/ScanTypes.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace vgmtrans::core {

namespace {

void normalizeItemTree(ItemTree& items, ScanIdAllocator& ids) {
  // Modules can return hand-authored or builder-created trees; normalize them before publishing.
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

    if (auto* parent = itemById(items, *item.parent)) {
      parent->children.push_back(item.id);
    } else {
      // Keep orphaned nodes inspectable instead of dropping source context.
      item.parent = std::nullopt;
      if (!firstRoot.has_value()) {
        firstRoot = item.id;
      }
    }
  }

  if (!items.root.has_value() || !itemById(items, *items.root)) {
    items.root = firstRoot;
  }
}

void assignMissingAssetIds(std::vector<Asset>& assets, ScanIdAllocator& ids) {
  // Format modules may assign IDs when cross-references are known during parsing. If
  // they do not, the shared scanner fills them in consistently before publishing assets.
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

Project scanProject(SourceStore& sources, const FormatRegistry& formats) {
  // A rescan starts from the user-provided sources. Any previously extracted sources are
  // regenerated so stale archive members or SPC RAM dumps cannot survive source changes.
  sources.discardVirtualizedTail();

  Project project;
  ScanIdAllocator ids;

  for (size_t sourceIndex = 0; sourceIndex < sources.sourceCount(); ++sourceIndex) {
    const auto source = sources.sourceAt(sourceIndex);
    const auto bytes = sources.bytes(source.id);

    for (const auto& module : formats.modules()) {
      bool shouldScan = false;
      try {
        // Probe failures are reported as diagnostics instead of aborting the whole scan;
        // one bad module should not hide assets that another module can still parse.
        shouldScan = module.canScan(source, bytes);
      } catch (const std::exception& ex) {
        project.diagnostics.push_back(errorDiagnostic(std::string(module.name) + " canScan failed: " + ex.what()));
      }

      if (!shouldScan) {
        continue;
      }

      try {
        // The ScanInput passes a shared ID allocator by reference so extracted child
        // sources and later modules can still receive project-wide stable IDs.
        ScanResult result = module.scan(ScanInput{
            .source = source,
            .reader = sources.reader(source.id),
            .ids = ids,
        });

        assignMissingAssetIds(result.assets, ids);
        assignMissingCollectionIds(result.collections, ids);

        project.assets.insert(project.assets.end(), std::make_move_iterator(result.assets.begin()),
                              std::make_move_iterator(result.assets.end()));
        project.collections.insert(project.collections.end(), std::make_move_iterator(result.collections.begin()),
                                   std::make_move_iterator(result.collections.end()));
        project.diagnostics.insert(project.diagnostics.end(), std::make_move_iterator(result.diagnostics.begin()),
                                   std::make_move_iterator(result.diagnostics.end()));

        for (auto& extracted : result.extractedSources) {
          // Extracted sources are appended and scanned by later loop iterations.
          extracted.file.virtualized = true;
          extracted.file.origin = extracted.origin;
          sources.add(std::move(extracted.file), std::move(extracted.bytes));
        }
      } catch (const std::exception& ex) {
        project.diagnostics.push_back(
            errorDiagnostic(std::string(module.name) + " scan failed: " + ex.what(),
                            SourceRange{.source = source.id, .offset = 0, .size = source.size}));
      }
    }
  }

  project.sources = sources.sourceFiles();
  return project;
}

}  // namespace vgmtrans::core
