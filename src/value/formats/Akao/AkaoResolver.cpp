/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"
#include "value/scan/AssetResolution.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

using SampleEntry = AssetWithData<SamplePoolAsset, AkaoSamplePoolData>;

void markCovered(std::set<u32>& remaining, const SampleEntry& sample) {
  for (const auto& articulation : sample.data->articulations) {
    if (articulation.sample.valid()) {
      remaining.erase(articulation.articulationId);
    }
  }
}

[[nodiscard]] bool psfLike(const SourceFile* source) {
  if (source == nullptr) {
    return false;
  }
  std::string ext = source->path.extension().string();
  std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return ext == ".psf" || ext == ".minipsf" || ext == ".psflib";
}

[[nodiscard]] std::string missingSampleMessage(const AkaoSamples& request) {
  if (request.sampleSetId) {
    return "Akao sequence references sample set " + std::to_string(*request.sampleSetId) +
           ", but no matching sample pool was found";
  }
  return "Akao sequence has no matching sample pool";
}

std::vector<SampleEntry> chooseSamples(const DependencyContext& context, const AkaoSamples& request,
                                       const std::vector<SampleEntry>& samples, std::set<u32>& remaining,
                                       DependencySelection& result) {
  std::vector<SampleEntry> candidates;
  const bool isolated = psfLike(context.source());
  const auto sequenceSource = context.metadata().range.source;
  for (const auto& sample : samples) {
    const bool sameSource = sequenceSource.valid() && sequenceSource == sample.sourceId();
    if (!isolated || sameSource) {
      candidates.push_back(sample);
    }
  }
  std::ranges::sort(candidates, [&](const SampleEntry& left, const SampleEntry& right) {
    const bool leftLocal = sequenceSource.valid() && left.sourceId() == sequenceSource;
    const bool rightLocal = sequenceSource.valid() && right.sourceId() == sequenceSource;
    return leftLocal != rightLocal ? leftLocal : left.id().value > right.id().value;
  });

  std::vector<SampleEntry> selected;
  const auto select = [&](const SampleEntry& sample) {
    selected.push_back(sample);
    markCovered(remaining, sample);
  };
  const auto requestedSampleSetId = request.sampleSetId;
  if (requestedSampleSetId && *requestedSampleSetId > 0) {
    const auto preferred = std::ranges::find_if(
        candidates, [&](const SampleEntry& sample) { return sample.data->sampleSetId == requestedSampleSetId; });
    if (preferred != candidates.end()) {
      select(*preferred);
    } else if (!isolated) {
      result.incomplete(missingSampleMessage(request), "missing-preferred-sample-set");
    }
  }

  // Honor the preferred set, then fill gaps with actual playable articulations
  // in source priority order. A declared ID range may contain rejected samples.
  for (const auto& candidate : candidates) {
    if (std::ranges::any_of(selected, [&](const SampleEntry& sample) { return sample.id() == candidate.id(); })) {
      continue;
    }
    const bool sameSampleSet = requestedSampleSetId.value_or(0) == candidate.data->sampleSetId.value_or(0);
    const bool contributes = std::ranges::any_of(candidate.data->articulations, [&](const auto& articulation) {
      return articulation.sample.valid() && remaining.contains(articulation.articulationId);
    });
    if ((!sameSampleSet || !selected.empty()) && !contributes) {
      continue;
    }
    select(candidate);
    if (remaining.empty()) {
      break;
    }
  }
  // Retain the source table ordering for stable binding of overlapping IDs.
  std::ranges::sort(selected, {}, [](const SampleEntry& sample) { return sample.data->firstArticulationId; });
  return selected;
}

}  // namespace

DependencySelection AkaoSamples::operator()(const DependencyContext& context) const {
  const auto samples = context.candidates<SamplePoolAsset, AkaoSamplePoolData>();
  std::set<u32> remaining(requiredArticulations.begin(), requiredArticulations.end());
  DependencySelection result;
  const auto selected = context.manual() ? samples : chooseSamples(context, *this, samples, remaining, result);
  for (const auto& sample : selected) {
    result.add(sample.id());
    markCovered(remaining, sample);
  }
  if (selected.empty()) {
    result.incomplete(missingSampleMessage(*this), "missing-sample-collection");
  } else if (!remaining.empty()) {
    std::string message = "Akao sample pools do not cover required articulation ids:";
    for (const u32 articulation : remaining) {
      message += " " + std::to_string(articulation);
    }
    result.incomplete(std::move(message), "missing-articulation-coverage");
  }
  return result;
}

void prepareAkaoBank(BankPreparationContext& context, const AkaoSoundBankData& data) {
  AkaoArticulationMap articulations;
  for (const auto& input : context.samples<AkaoSamplePoolData>()) {
    for (const auto& articulation : input.data.articulations) {
      if (articulation.sample.valid()) {
        articulations[articulation.articulationId] = articulation;
      }
    }
  }
  if (!applyAkaoArticulations(context.bank, data.binding, articulations)) {
    context.fail("Akao retained instrument recipe does not match its structural bank");
  }

  std::set<u32> missing;
  for (const auto& regions : data.binding.regions) {
    for (const auto& region : regions) {
      if (region.articulationId != 0 && !articulations.contains(region.articulationId)) {
        missing.insert(region.articulationId);
      }
    }
  }
  if (!missing.empty()) {
    std::string message = "Akao collection does not provide required articulations:";
    for (const u32 articulation : missing) {
      message += " " + std::to_string(articulation);
    }
    context.warning(std::move(message));
  }
}

}  // namespace vgmtrans::formats::akao
