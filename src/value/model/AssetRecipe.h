/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/CollectionModel.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::core {

class DependencyContext;
struct BankPreparationContext;
struct SequencePreparationContext;
using SequencePreparer = std::function<void(SequencePreparationContext&)>;

struct DependencySelection {
  std::vector<DependencyTarget> targets;
  std::vector<AssetId> alternatives;
  std::vector<CollectionIssue> issues;

  DependencySelection& add(AssetId id, AssetPrivateData placement = {}) {
    targets.push_back({id, std::move(placement)});
    return *this;
  }
};

using DependencySelector = std::function<DependencySelection(const DependencyContext&)>;
using BankPreparer = std::function<void(BankPreparationContext&)>;

struct AssetDependency {
  DependencyRole role = DependencyRole::SoundBank;
  DependencySelector select;
};

// Immutable instructions published with an asset. Selection is deferred until
// the session has admitted its sources; preparation runs only on export copies.
struct AssetRecipe {
  // A nonempty namespace makes this asset an automatic collection root. The
  // core derives its identity from AssetId, independently of names or matches.
  std::string collectionNamespace;
  std::vector<AssetDependency> dependencies;
};

}  // namespace vgmtrans::core
