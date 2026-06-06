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

struct VGMMetadataHint {
  std::string targetFormat;
  std::string sourceFormat;
  std::filesystem::path sourcePath;
  VGMTag tag;
  std::optional<u32> songIndex;
  std::optional<u32> romAddress;
  std::optional<u32> fileOffset;
  std::optional<std::string> lookupKey;
};

struct VGMMetadataHintQuery {
  std::string targetFormat;
  std::optional<u32> songIndex;
  std::optional<u32> romAddress;
  std::optional<u32> fileOffset;
  std::optional<std::string> lookupKey;
};

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
      : m_hints(std::move(hints)) {
    buildIndexes();
  }

  [[nodiscard]] const VGMMetadataHint* findHint(
      const VGMMetadataHintQuery& query) const override {
    if (query.songIndex) {
      if (const auto* hint = findIndexed(m_bySongIndex, query.targetFormat, *query.songIndex);
          hint && matchesQuery(*hint, query)) {
        return hint;
      }
    }

    if (query.romAddress) {
      if (const auto* hint = findIndexed(m_byRomAddress, query.targetFormat, *query.romAddress);
          hint && matchesQuery(*hint, query)) {
        return hint;
      }
    }

    if (query.fileOffset) {
      if (const auto* hint = findIndexed(m_byFileOffset, query.targetFormat, *query.fileOffset);
          hint && matchesQuery(*hint, query)) {
        return hint;
      }
    }

    if (query.lookupKey) {
      if (const auto* hint = findIndexed(m_byLookupKey, query.targetFormat, *query.lookupKey);
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

  std::vector<VGMMetadataHint> m_hints;
  Index m_bySongIndex;
  Index m_byRomAddress;
  Index m_byFileOffset;
  StringIndex m_byLookupKey;

  static bool matchesQuery(const VGMMetadataHint& hint, const VGMMetadataHintQuery& query) {
    if (!query.targetFormat.empty() && hint.targetFormat != query.targetFormat) {
      return false;
    }
    if (query.songIndex && hint.songIndex != query.songIndex) {
      return false;
    }
    if (query.romAddress && hint.romAddress != query.romAddress) {
      return false;
    }
    if (query.fileOffset && hint.fileOffset != query.fileOffset) {
      return false;
    }
    if (query.lookupKey && hint.lookupKey != query.lookupKey) {
      return false;
    }
    return true;
  }

  void buildIndexes() {
    for (size_t i = 0; i < m_hints.size(); i++) {
      const auto& hint = m_hints[i];
      if (hint.songIndex) {
        m_bySongIndex[hint.targetFormat].try_emplace(*hint.songIndex, i);
      }
      if (hint.romAddress) {
        m_byRomAddress[hint.targetFormat].try_emplace(*hint.romAddress, i);
      }
      if (hint.fileOffset) {
        m_byFileOffset[hint.targetFormat].try_emplace(*hint.fileOffset, i);
      }
      if (hint.lookupKey) {
        m_byLookupKey[hint.targetFormat].try_emplace(*hint.lookupKey, i);
      }
    }
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
