/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/CollectionModel.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace vgmtrans::core {

class DependencyContext;
class BankAssignmentContext;
struct BankPreparationContext;
struct SequencePreparationContext;
struct SequenceRuntime;
// Return a configured runtime, or nullopt to keep the scanned runtime.
using SequencePreparer = std::function<std::optional<SequenceRuntime>(SequencePreparationContext&)>;

// The answer to one input request. Chosen inputs are separate from alternatives
// so an unresolved choice is never mistaken for a group that should play together.
class DependencySelection {
public:
  [[nodiscard]] ResolutionStatus status() const {
    return status_ == ResolutionStatus::Resolved && targets_.empty() ? ResolutionStatus::Incomplete : status_;
  }
  [[nodiscard]] const std::vector<DependencyTarget>& targets() const { return targets_; }
  [[nodiscard]] const std::vector<DependencyTarget>& alternatives() const { return alternatives_; }
  [[nodiscard]] const std::vector<CollectionIssue>& issues() const { return issues_; }

  // An empty selection is incomplete. Adding inputs resolves it unless the
  // request has already reported missing coverage, ambiguity, or failure.
  DependencySelection& add(AssetId id, AssetPrivateData placement = {}) {
    targets_.push_back({id, std::move(placement)});
    return *this;
  }

  DependencySelection& incomplete(std::string message, std::string code = "incomplete-dependency") {
    if (status_ == ResolutionStatus::Resolved) {
      status_ = ResolutionStatus::Incomplete;
    }
    issues_.push_back({.severity = Severity::Warning, .code = std::move(code), .message = std::move(message)});
    return *this;
  }

  // Keep any targets the format deliberately chose as a fallback. Alternatives
  // describe the unresolved choices, including different positions in one pool.
  DependencySelection& ambiguous(std::vector<DependencyTarget> alternatives, std::string message) {
    if (status_ != ResolutionStatus::Failed) {
      status_ = ResolutionStatus::Ambiguous;
    }
    alternatives_ = std::move(alternatives);
    issues_.push_back(ambiguousMatchIssue(std::move(message)));
    return *this;
  }

  [[nodiscard]] static DependencySelection failed(std::string message) {
    DependencySelection result;
    result.status_ = ResolutionStatus::Failed;
    result.issues_.push_back(
        {.severity = Severity::Error, .code = "dependency-resolution-failed", .message = std::move(message)});
    return result;
  }

private:
  ResolutionStatus status_ = ResolutionStatus::Resolved;
  std::vector<DependencyTarget> targets_;
  std::vector<DependencyTarget> alternatives_;
  std::vector<CollectionIssue> issues_;
};

using DependencySelector = std::function<DependencySelection(const DependencyContext&)>;
using BankPreparer = std::function<void(BankPreparationContext&)>;
using BankAssigner = std::function<void(BankAssignmentContext&)>;

// Use an asset ID when scanning already found the input. Otherwise, a callback
// chooses from the available assets when collections are rebuilt.
using DependencyRequest = std::variant<DependencyTarget, DependencySelector>;

// Recipes describe which inputs are needed. Preparation callbacks later use
// those inputs to make the sequence or bank ready for playback and export.
struct SequenceRecipe {
  std::vector<DependencyRequest> banks;
  // Give the chosen banks settings for this sequence, such as bank numbers.
  // This cannot change which banks were chosen or edit the banks themselves.
  BankAssigner assignBanks;
};

struct BankRecipe {
  std::vector<DependencyRequest> samples;
};

}  // namespace vgmtrans::core
