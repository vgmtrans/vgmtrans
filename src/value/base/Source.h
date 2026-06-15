/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/base/CoreTypes.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vgmtrans::core {

enum class SourceKind {
  UserLoaded,
  Derived,
};

struct SourceFile {
  SourceId id;
  SourceKind kind = SourceKind::UserLoaded;
  std::string name;
  std::optional<std::string> title;
  std::filesystem::path path;
  u64 size = 0;
  // Derived sources are real session entries, such as archive members, SPC RAM,
  // or PSF executable images. parent/origin record where they came from.
  std::optional<SourceId> parent;
  std::optional<SourceRange> origin;

  [[nodiscard]] bool derived() const noexcept { return kind == SourceKind::Derived; }
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

  // Reads throw on out-of-range access. Use has() first when malformed data should
  // stop parsing without an exception.
  [[nodiscard]] u8 u8At(u64 offset) const;
  [[nodiscard]] s8 s8At(u64 offset) const;
  [[nodiscard]] u16 le16(u64 offset) const;
  [[nodiscard]] u16 be16(u64 offset) const;
  [[nodiscard]] u32 le32(u64 offset) const;
  [[nodiscard]] u32 be32(u64 offset) const;
  [[nodiscard]] std::span<const u8> slice(SourceRange range) const;
  [[nodiscard]] std::span<const u8> slice(u64 offset, u64 size) const;

private:
  void require(u64 offset, u64 size) const;

  // ByteReader does not own bytes; SourceStore owns storage for the reader lifetime.
  SourceId source_;
  std::span<const u8> bytes_;
};

struct ExtractedSource {
  // Format modules return extracted bytes here. Session appends them as derived
  // sources so they can be inspected and scanned like user-loaded files.
  SourceFile file;
  std::vector<u8> bytes;
  std::optional<SourceRange> origin;
};

class SourceStore {
public:
  // SourceStore owns all bytes referenced by SourceRange. Assets copy SourceRange
  // values instead of copying source bytes.
  SourceId add(SourceFile file, std::vector<u8> bytes);
  SourceId addDerived(SourceFile file, std::vector<u8> bytes, SourceId parent, std::optional<SourceRange> origin);

  [[nodiscard]] bool contains(SourceId id) const noexcept;
  [[nodiscard]] std::span<const u8> bytes(SourceId id) const;
  [[nodiscard]] ByteReader reader(SourceId id) const;
  [[nodiscard]] const SourceFile& source(SourceId id) const;
  [[nodiscard]] const SourceFile& sourceAt(size_t index) const;
  [[nodiscard]] size_t sourceCount() const noexcept { return entries_.size(); }
  [[nodiscard]] std::vector<SourceFile> sourceFiles() const;

private:
  struct Entry {
    SourceFile file;
    std::vector<u8> bytes;
  };

  [[nodiscard]] const Entry& entry(SourceId id) const;

  std::vector<Entry> entries_;
};

}  // namespace vgmtrans::core
