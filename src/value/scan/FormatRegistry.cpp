/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/FormatRegistry.h"

#include <fmt/format.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

void validateAcceptedFormats(std::string_view processorName, const std::vector<std::string>& formats) {
  std::unordered_set<std::string_view> provided;
  provided.reserve(formats.size());
  for (const auto& format : formats) {
    if (format.empty()) {
      throw std::invalid_argument("Cannot register an empty accepted source format");
    }
    if (!provided.insert(format).second) {
      throw std::invalid_argument(fmt::format("Duplicate source format accepted by {}: {}", processorName, format));
    }
  }
}

}  // namespace

void FormatRegistry::add(FormatModule module) {
  if (sealed_) {
    throw std::logic_error("Cannot register value formats after session mutation has started");
  }
  if (module.name.empty() || !module.scan) {
    throw std::invalid_argument("Cannot register an incomplete FormatModule");
  }
  if (std::ranges::find(modules_, module.name, &FormatModule::name) != modules_.end()) {
    throw std::invalid_argument(fmt::format("Duplicate FormatModule name: {}", module.name));
  }
  if (module.resolveCollections) {
    const auto duplicate = std::ranges::find_if(modules_, [&](const FormatModule& registered) {
      return registered.resolveCollections && registered.collectionResolver() == module.collectionResolver();
    });
    if (duplicate != modules_.end()) {
      throw std::invalid_argument(fmt::format("Duplicate collection resolver for {}: {} and {}",
                                              module.collectionResolver(), duplicate->name, module.name));
    }
  }
  if (module.bindCollection) {
    const auto duplicate = std::ranges::find_if(modules_, [&](const FormatModule& registered) {
      return registered.bindCollection && registered.collectionResolver() == module.collectionResolver();
    });
    if (duplicate != modules_.end()) {
      throw std::invalid_argument(fmt::format("Duplicate collection binder for resolver {}: {} and {}",
                                              module.collectionResolver(), duplicate->name, module.name));
    }
  }

  validateAcceptedFormats(module.name, module.acceptedFormats);
  modules_.push_back(std::move(module));
}

void FormatRegistry::add(SourceExtractor extractor) {
  if (sealed_) {
    throw std::logic_error("Cannot register source extractors after session mutation has started");
  }
  if (extractor.name.empty() || !extractor.extract) {
    throw std::invalid_argument("Cannot register an incomplete SourceExtractor");
  }
  if (std::ranges::find(extractors_, extractor.name, &SourceExtractor::name) != extractors_.end()) {
    throw std::invalid_argument(fmt::format("Duplicate SourceExtractor name: {}", extractor.name));
  }
  validateAcceptedFormats(extractor.name, extractor.acceptedFormats);
  extractors_.push_back(std::move(extractor));
}

void FormatRegistry::seal() noexcept {
  sealed_ = true;
}

const FormatModule* FormatRegistry::findModule(std::string_view name) const {
  const auto found = std::ranges::find(modules_, name, &FormatModule::name);
  return found != modules_.end() ? &*found : nullptr;
}

CollectionBinder FormatRegistry::collectionBinder(std::string_view resolver) const {
  const auto found = std::ranges::find_if(modules_, [&](const FormatModule& module) {
    return module.bindCollection && module.collectionResolver() == resolver;
  });
  return found != modules_.end() ? found->bindCollection : CollectionBinder{};
}

CollectionBinder FormatRegistry::collectionBinderForFormat(std::string_view format) const {
  const auto* module = findModule(format);
  return module != nullptr ? collectionBinder(module->collectionResolver()) : CollectionBinder{};
}

}  // namespace vgmtrans::core
