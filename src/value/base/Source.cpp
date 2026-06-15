/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/base/Source.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

ByteReader::ByteReader(SourceId source, std::span<const u8> bytes) : source_(source), bytes_(bytes) {
}

bool ByteReader::has(u64 offset, u64 size) const noexcept {
  if (offset > bytes_.size()) {
    return false;
  }
  return size <= bytes_.size() - offset;
}

SourceRange ByteReader::range(u64 offset, u64 size) const noexcept {
  return SourceRange{.source = source_, .offset = offset, .size = size};
}

u8 ByteReader::u8At(u64 offset) const {
  require(offset, 1);
  return bytes_[offset];
}

s8 ByteReader::s8At(u64 offset) const {
  return static_cast<s8>(u8At(offset));
}

u16 ByteReader::le16(u64 offset) const {
  require(offset, 2);
  return static_cast<u16>(bytes_[offset] | (bytes_[offset + 1] << 8));
}

u16 ByteReader::be16(u64 offset) const {
  require(offset, 2);
  return static_cast<u16>((bytes_[offset] << 8) | bytes_[offset + 1]);
}

u32 ByteReader::le32(u64 offset) const {
  require(offset, 4);
  return static_cast<u32>(bytes_[offset]) | (static_cast<u32>(bytes_[offset + 1]) << 8) |
         (static_cast<u32>(bytes_[offset + 2]) << 16) | (static_cast<u32>(bytes_[offset + 3]) << 24);
}

u32 ByteReader::be32(u64 offset) const {
  require(offset, 4);
  return (static_cast<u32>(bytes_[offset]) << 24) | (static_cast<u32>(bytes_[offset + 1]) << 16) |
         (static_cast<u32>(bytes_[offset + 2]) << 8) | static_cast<u32>(bytes_[offset + 3]);
}

std::span<const u8> ByteReader::slice(SourceRange range) const {
  if (range.source != source_) {
    throw std::out_of_range("SourceRange belongs to a different source");
  }
  return slice(range.offset, range.size);
}

std::span<const u8> ByteReader::slice(u64 offset, u64 size) const {
  require(offset, size);
  return bytes_.subspan(offset, size);
}

void ByteReader::require(u64 offset, u64 size) const {
  if (!has(offset, size)) {
    throw std::out_of_range("ByteReader access outside source bounds");
  }
}

SourceId SourceStore::add(SourceFile file, std::vector<u8> bytes) {
  const auto id = SourceId{static_cast<u32>(entries_.size())};
  file.id = id;
  file.size = bytes.size();
  if (file.name.empty() && !file.path.empty()) {
    file.name = file.path.filename().string();
  }
  entries_.push_back(Entry{.file = std::move(file), .bytes = std::move(bytes)});
  return id;
}

SourceId SourceStore::addDerived(SourceFile file, std::vector<u8> bytes, SourceId parent,
                                 std::optional<SourceRange> origin) {
  if (!parent.valid()) {
    throw std::invalid_argument("Derived source requires a valid parent SourceId");
  }

  file.kind = SourceKind::Derived;
  file.parent = parent;
  file.origin = origin;
  return add(std::move(file), std::move(bytes));
}

bool SourceStore::contains(SourceId id) const noexcept {
  return id.valid() && id.value < entries_.size();
}

std::span<const u8> SourceStore::bytes(SourceId id) const {
  const auto& e = entry(id);
  return e.bytes;
}

ByteReader SourceStore::reader(SourceId id) const {
  return ByteReader{id, bytes(id)};
}

const SourceFile& SourceStore::source(SourceId id) const {
  return entry(id).file;
}

const SourceFile& SourceStore::sourceAt(size_t index) const {
  if (index >= entries_.size()) {
    throw std::out_of_range("Source index outside SourceStore bounds");
  }
  return entries_[index].file;
}

std::vector<SourceFile> SourceStore::sourceFiles() const {
  std::vector<SourceFile> files;
  files.reserve(entries_.size());
  for (const auto& e : entries_) {
    files.push_back(e.file);
  }
  return files;
}

const SourceStore::Entry& SourceStore::entry(SourceId id) const {
  if (!contains(id)) {
    throw std::out_of_range("SourceId is not present in SourceStore");
  }
  return entries_[id.value];
}

}  // namespace vgmtrans::core
