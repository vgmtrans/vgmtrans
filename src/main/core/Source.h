/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "core/CoreTypes.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vgmtrans::core {

struct SourceFile {
  SourceId id;
  std::string name;
  std::filesystem::path path;
  u64 size = 0;
  bool virtualized = false;
  std::optional<SourceRange> origin;
};

class ByteReader {
 public:
  ByteReader() = default;
  ByteReader(SourceId source, std::span<const u8> bytes);

  [[nodiscard]] SourceId source() const noexcept { return source_; }
  [[nodiscard]] u64 size() const noexcept { return bytes_.size(); }
  [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }
  [[nodiscard]] bool has(u64 offset, u64 size) const noexcept;
  [[nodiscard]] SourceRange range(u64 offset, u64 size) const noexcept;

  [[nodiscard]] u8 u8At(u64 offset) const;
  [[nodiscard]] u16 le16(u64 offset) const;
  [[nodiscard]] u16 be16(u64 offset) const;
  [[nodiscard]] u32 le32(u64 offset) const;
  [[nodiscard]] u32 be32(u64 offset) const;
  [[nodiscard]] std::span<const u8> slice(SourceRange range) const;
  [[nodiscard]] std::span<const u8> slice(u64 offset, u64 size) const;

 private:
  void require(u64 offset, u64 size) const;

  SourceId source_;
  std::span<const u8> bytes_;
};

struct ExtractedSource {
  SourceFile file;
  std::vector<u8> bytes;
  std::optional<SourceRange> origin;
};

class SourceStore {
 public:
  SourceId add(SourceFile file, std::vector<u8> bytes);

  [[nodiscard]] bool contains(SourceId id) const noexcept;
  [[nodiscard]] std::span<const u8> bytes(SourceId id) const;
  [[nodiscard]] ByteReader reader(SourceId id) const;
  [[nodiscard]] const SourceFile& source(SourceId id) const;
  [[nodiscard]] const SourceFile& sourceAt(size_t index) const;
  [[nodiscard]] size_t sourceCount() const noexcept { return entries_.size(); }
  [[nodiscard]] std::vector<SourceFile> sourceFiles() const;

  void discardVirtualizedTail();

 private:
  struct Entry {
    SourceFile file;
    std::vector<u8> bytes;
  };

  [[nodiscard]] const Entry& entry(SourceId id) const;

  std::vector<Entry> entries_;
};

}  // namespace vgmtrans::core
