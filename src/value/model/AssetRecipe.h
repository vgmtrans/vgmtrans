/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/CollectionModel.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace vgmtrans::core {

class DependencyContext;
struct BankPreparationContext;
struct SequencePreparationContext;
struct SequenceRuntime;
// Return a configured runtime, or nullopt to keep the scanned runtime.
using SequencePreparer = std::function<std::optional<SequenceRuntime>(SequencePreparationContext&)>;

// The answer to one input request. A format can report ambiguity while
// deliberately choosing fallback inputs.
class DependencySelection {
public:
  [[nodiscard]] ResolutionStatus status() const {
    return status_ == ResolutionStatus::Resolved && targets_.empty() ? ResolutionStatus::Incomplete : status_;
  }
  [[nodiscard]] const std::vector<DependencyTarget>& targets() const { return targets_; }
  [[nodiscard]] const std::vector<Diagnostic>& issues() const { return issues_; }

  // An empty selection is incomplete. Adding inputs resolves it unless the
  // request has already reported missing coverage, ambiguity, or failure.
  DependencySelection& add(AssetId id, AssetPrivateData placement = {}) {
    targets_.push_back({id, std::move(placement)});
    return *this;
  }

  DependencySelection& incomplete(std::string message, std::string code = "incomplete-dependency") {
    status_ = std::max(status_, ResolutionStatus::Incomplete);
    issues_.push_back({.severity = Severity::Warning, .code = std::move(code), .message = std::move(message)});
    return *this;
  }

  // Keep any targets the format deliberately chose as a fallback.
  DependencySelection& ambiguous(std::string message) {
    status_ = std::max(status_, ResolutionStatus::Ambiguous);
    issues_.push_back({.severity = Severity::Warning, .code = "ambiguous-match", .message = std::move(message)});
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
  std::vector<Diagnostic> issues_;
};

using DependencySelector = std::function<DependencySelection(const DependencyContext&)>;
using BankPreparer = std::function<void(BankPreparationContext&)>;

// Use an asset ID when scanning already found the input. Otherwise, a callback
// chooses from the available assets when collections are rebuilt.
using DependencyRequest = std::variant<DependencyTarget, DependencySelector>;

}  // namespace vgmtrans::core
