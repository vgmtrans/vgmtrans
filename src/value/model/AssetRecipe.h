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
#include <variant>
#include <vector>

namespace vgmtrans::core {

class DependencyContext;
class BankAssignmentContext;
struct BankPreparationContext;
struct SequencePreparationContext;
using SequencePreparer = std::function<void(SequencePreparationContext&)>;

class DependencySelection {
public:
  [[nodiscard]] ResolutionStatus status() const {
    return status_ == ResolutionStatus::Resolved && targets_.empty() ? ResolutionStatus::Incomplete : status_;
  }
  [[nodiscard]] const std::vector<DependencyTarget>& targets() const { return targets_; }
  [[nodiscard]] const std::vector<DependencyTarget>& alternatives() const { return alternatives_; }
  [[nodiscard]] const std::vector<CollectionIssue>& issues() const { return issues_; }

  // An empty selection is incomplete. Adding providers resolves it unless the
  // request has explicitly reported incomplete coverage or ambiguity.
  DependencySelection& add(AssetId id, AssetPrivateData placement = {}) {
    targets_.push_back({id, std::move(placement)});
    return *this;
  }

  DependencySelection& incomplete(std::string message, std::string code = "incomplete-dependency") {
    if (status_ == ResolutionStatus::Resolved) {
      status_ = ResolutionStatus::Incomplete;
    }
    issues_.push_back({.impact = CollectionIssueImpact::Incomplete,
                       .severity = Severity::Warning,
                       .code = std::move(code),
                       .message = std::move(message)});
    return *this;
  }

  // Selected targets, when present, are an explicit format-chosen fallback.
  // Alternatives retain placements, including different positions in one pool.
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
    result.issues_.push_back({.impact = CollectionIssueImpact::Incomplete,
                              .severity = Severity::Error,
                              .code = "dependency-resolution-failed",
                              .message = std::move(message)});
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

// Scanner-known references are values; only genuinely deferred matching needs
// a callback. The owning recipe determines the provider type.
using DependencyRequest = std::variant<DependencyTarget, DependencySelector>;

struct SequenceRecipe {
  std::vector<DependencyRequest> banks;
  // Optional sequence-specific meaning of the selected banks. It can attach
  // placement values, but cannot change membership or mutate the banks.
  BankAssigner assignBanks;
};

struct BankRecipe {
  std::vector<DependencyRequest> samples;
};

}  // namespace vgmtrans::core
