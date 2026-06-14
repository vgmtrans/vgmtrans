/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/model/ProjectModel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <variant>

namespace vgmtrans::core {

namespace {

[[nodiscard]] Diagnostic projectError(std::string message) {
  return Diagnostic{
      .severity = Severity::Error,
      .message = std::move(message),
  };
}

}  // namespace

std::vector<Diagnostic> CollectionAssetDiagnostics::all() const {
  std::vector<Diagnostic> diagnostics;
  diagnostics.reserve(collection.size() + sequence.size() + instrumentSets.size() + sampleCollections.size() +
                      miscAssets.size());
  diagnostics.insert(diagnostics.end(), collection.begin(), collection.end());
  diagnostics.insert(diagnostics.end(), sequence.begin(), sequence.end());
  diagnostics.insert(diagnostics.end(), instrumentSets.begin(), instrumentSets.end());
  diagnostics.insert(diagnostics.end(), sampleCollections.begin(), sampleCollections.end());
  diagnostics.insert(diagnostics.end(), miscAssets.begin(), miscAssets.end());
  return diagnostics;
}

AssetMetadata& metadata(Asset& asset) {
  return std::visit([](auto& typedAsset) -> AssetMetadata& { return typedAsset.metadata; }, asset);
}

const AssetMetadata& metadata(const Asset& asset) {
  return std::visit([](const auto& typedAsset) -> const AssetMetadata& { return typedAsset.metadata; }, asset);
}

ProjectIndex buildProjectIndex(const Project& project) {
  ProjectIndex index;
  index.assetsById.reserve(project.assets.size());
  for (size_t i = 0; i < project.assets.size(); ++i) {
    const AssetId id = metadata(project.assets[i]).id;
    if (id.valid()) {
      index.assetsById.emplace(id.value, i);
    }
  }

  index.collectionsById.reserve(project.collections.size());
  for (size_t i = 0; i < project.collections.size(); ++i) {
    const CollectionId id = project.collections[i].id;
    if (id.valid()) {
      index.collectionsById.emplace(id.value, i);
    }
  }

  index.valid = true;
  return index;
}

void rebuildProjectIndex(Project& project) {
  project.index = buildProjectIndex(project);
}

ItemNode* itemById(ItemTree& tree, ItemId id) {
  const auto found = std::ranges::find_if(tree.nodes, [id](const ItemNode& item) { return item.id == id; });
  if (found == tree.nodes.end()) {
    return nullptr;
  }
  return &*found;
}

const ItemNode* itemById(const ItemTree& tree, ItemId id) {
  const auto found = std::ranges::find_if(tree.nodes, [id](const ItemNode& item) { return item.id == id; });
  if (found == tree.nodes.end()) {
    return nullptr;
  }
  return &*found;
}

Asset* assetById(Project& project, AssetId id) {
  if (project.index.valid) {
    const auto found = project.index.assetsById.find(id.value);
    if (found == project.index.assetsById.end() || found->second >= project.assets.size()) {
      return nullptr;
    }
    return &project.assets[found->second];
  }

  const auto found =
      std::ranges::find_if(project.assets, [id](const Asset& asset) { return metadata(asset).id == id; });
  if (found == project.assets.end()) {
    return nullptr;
  }
  return &*found;
}

const Asset* assetById(const Project& project, AssetId id) {
  if (project.index.valid) {
    const auto found = project.index.assetsById.find(id.value);
    if (found == project.index.assetsById.end() || found->second >= project.assets.size()) {
      return nullptr;
    }
    return &project.assets[found->second];
  }

  const auto found =
      std::ranges::find_if(project.assets, [id](const Asset& asset) { return metadata(asset).id == id; });
  if (found == project.assets.end()) {
    return nullptr;
  }
  return &*found;
}

const Collection* collectionById(const Project& project, CollectionId id) {
  if (project.index.valid) {
    const auto found = project.index.collectionsById.find(id.value);
    if (found == project.index.collectionsById.end() || found->second >= project.collections.size()) {
      return nullptr;
    }
    return &project.collections[found->second];
  }

  const auto found =
      std::ranges::find_if(project.collections, [id](const Collection& collection) { return collection.id == id; });
  if (found == project.collections.end()) {
    return nullptr;
  }
  return &*found;
}

CollectionAssets resolveCollectionAssets(const Project& project, CollectionId id) {
  if (const auto* collection = collectionById(project, id)) {
    return resolveCollectionAssets(project, *collection);
  }

  CollectionAssets resolved;
  resolved.diagnostics.collection.push_back(projectError("CollectionId was not found in the Project snapshot"));
  return resolved;
}

CollectionAssets resolveCollectionAssets(const Project& project, const Collection& collection) {
  // Export code should not have to duplicate ID lookup and diagnostic policy. Resolve all
  // optional references once and return the usable assets plus any broken-reference errors.
  CollectionAssets resolved{
      .collection = &collection,
  };

  if (collection.sequence) {
    if (const auto* sequenceProgram = assetById<SequenceProgramAsset>(project, *collection.sequence)) {
      resolved.sequenceProgram = sequenceProgram;
    } else {
      resolved.diagnostics.sequence.push_back(projectError("Collection sequence asset was not found"));
    }
  }

  resolved.instrumentSets.reserve(collection.instrumentSets.size());
  for (const auto id : collection.instrumentSets) {
    if (const auto* instrumentSet = assetById<InstrumentSetAsset>(project, id)) {
      resolved.instrumentSets.push_back(instrumentSet);
    } else {
      resolved.diagnostics.instrumentSets.push_back(projectError("Collection instrument set asset was not found"));
    }
  }

  resolved.sampleCollections.reserve(collection.sampleCollections.size());
  for (const auto id : collection.sampleCollections) {
    if (const auto* sampleCollection = assetById<SampleCollectionAsset>(project, id)) {
      resolved.sampleCollections.push_back(sampleCollection);
    } else {
      resolved.diagnostics.sampleCollections.push_back(
          projectError("Collection sample collection asset was not found"));
    }
  }

  resolved.miscAssets.reserve(collection.miscAssets.size());
  for (const auto id : collection.miscAssets) {
    if (const auto* misc = assetById<MiscAsset>(project, id)) {
      resolved.miscAssets.push_back(misc);
    } else {
      resolved.diagnostics.miscAssets.push_back(projectError("Collection misc asset was not found"));
    }
  }

  return resolved;
}

}  // namespace vgmtrans::core
