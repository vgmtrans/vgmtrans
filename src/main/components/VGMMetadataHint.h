/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"
#include "VGMTag.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct VGMMetadataHintTarget {
  std::string targetFormat{};
  std::optional<u32> songIndex = std::nullopt;
  std::optional<u32> romAddress = std::nullopt;
  std::optional<u32> fileOffset = std::nullopt;
  std::optional<std::string> lookupKey = std::nullopt;
};

struct VGMMetadataHint {
  VGMMetadataHintTarget target{};
  std::string sourceFormat{};
  std::filesystem::path sourcePath{};
  VGMTag tag{};
};

using VGMMetadataHintQuery = VGMMetadataHintTarget;

class VGMMetadataHintProvider {
public:
  virtual ~VGMMetadataHintProvider() = default;

  [[nodiscard]] virtual const VGMMetadataHint* findHint(
      const VGMMetadataHintQuery& query) const = 0;
  [[nodiscard]] virtual const std::vector<VGMMetadataHint>& allHints() const = 0;
};

class IndexedMetadataHintProvider final : public VGMMetadataHintProvider {
public:
  explicit IndexedMetadataHintProvider(std::vector<VGMMetadataHint> hints)
      : m_hints(std::move(hints)), m_indexes(buildIndexes(m_hints)) {}

  [[nodiscard]] const VGMMetadataHint* findHint(
      const VGMMetadataHintQuery& query) const override {
    if (query.songIndex) {
      if (const auto* hint = findIndexed(m_indexes.bySongIndex, query.targetFormat, *query.songIndex);
          hint && matchesQuery(*hint, query)) {
        return hint;
      }
    }

    if (query.romAddress) {
      if (const auto* hint = findIndexed(m_indexes.byRomAddress, query.targetFormat, *query.romAddress);
          hint && matchesQuery(*hint, query)) {
        return hint;
      }
    }

    if (query.fileOffset) {
      if (const auto* hint = findIndexed(m_indexes.byFileOffset, query.targetFormat, *query.fileOffset);
          hint && matchesQuery(*hint, query)) {
        return hint;
      }
    }

    if (query.lookupKey) {
      if (const auto* hint = findIndexed(m_indexes.byLookupKey, query.targetFormat, *query.lookupKey);
          hint && matchesQuery(*hint, query)) {
        return hint;
      }
    }

    for (const auto& hint : m_hints) {
      if (matchesQuery(hint, query)) {
        return &hint;
      }
    }

    return nullptr;
  }

  [[nodiscard]] const std::vector<VGMMetadataHint>& allHints() const override {
    return m_hints;
  }

private:
  using Index = std::unordered_map<std::string, std::unordered_map<u32, size_t>>;
  using StringIndex = std::unordered_map<std::string, std::unordered_map<std::string, size_t>>;

  struct Indexes {
    Index bySongIndex;
    Index byRomAddress;
    Index byFileOffset;
    StringIndex byLookupKey;
  };

  const std::vector<VGMMetadataHint> m_hints;
  const Indexes m_indexes;

  static bool matchesQuery(const VGMMetadataHint& hint, const VGMMetadataHintQuery& query) {
    if (!query.targetFormat.empty() && hint.target.targetFormat != query.targetFormat) {
      return false;
    }
    if (query.songIndex && hint.target.songIndex != query.songIndex) {
      return false;
    }
    if (query.romAddress && hint.target.romAddress != query.romAddress) {
      return false;
    }
    if (query.fileOffset && hint.target.fileOffset != query.fileOffset) {
      return false;
    }
    if (query.lookupKey && hint.target.lookupKey != query.lookupKey) {
      return false;
    }
    return true;
  }

  static Indexes buildIndexes(const std::vector<VGMMetadataHint>& hints) {
    Indexes indexes;
    for (size_t i = 0; i < hints.size(); i++) {
      const auto& hint = hints[i];
      if (hint.target.songIndex) {
        indexes.bySongIndex[hint.target.targetFormat].try_emplace(*hint.target.songIndex, i);
      }
      if (hint.target.romAddress) {
        indexes.byRomAddress[hint.target.targetFormat].try_emplace(*hint.target.romAddress, i);
      }
      if (hint.target.fileOffset) {
        indexes.byFileOffset[hint.target.targetFormat].try_emplace(*hint.target.fileOffset, i);
      }
      if (hint.target.lookupKey) {
        indexes.byLookupKey[hint.target.targetFormat].try_emplace(*hint.target.lookupKey, i);
      }
    }
    return indexes;
  }

  [[nodiscard]] const VGMMetadataHint* findIndexed(
      const Index& index, const std::string& targetFormat, u32 key) const {
    const auto formatIt = index.find(targetFormat);
    if (formatIt == index.end()) {
      return nullptr;
    }

    const auto valueIt = formatIt->second.find(key);
    if (valueIt == formatIt->second.end()) {
      return nullptr;
    }

    return &m_hints[valueIt->second];
  }

  [[nodiscard]] const VGMMetadataHint* findIndexed(
      const StringIndex& index, const std::string& targetFormat, const std::string& key) const {
    const auto formatIt = index.find(targetFormat);
    if (formatIt == index.end()) {
      return nullptr;
    }

    const auto valueIt = formatIt->second.find(key);
    if (valueIt == formatIt->second.end()) {
      return nullptr;
    }

    return &m_hints[valueIt->second];
  }
};
