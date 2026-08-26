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

std::optional<std::string_view> SourceSegment::attribute(std::string_view key) const noexcept {
  const auto found = attributes.find(key);
  return found != attributes.end() ? std::optional<std::string_view>{found->second} : std::nullopt;
}

std::optional<std::string_view> SourceFile::attribute(std::string_view key) const noexcept {
  const auto found = attributes.find(key);
  return found != attributes.end() ? std::optional<std::string_view>{found->second} : std::nullopt;
}

const SourceSegment* SourceFile::segment(std::string_view segmentName) const noexcept {
  const auto found = std::ranges::find(segments, segmentName, &SourceSegment::name);
  return found != segments.end() ? std::addressof(*found) : nullptr;
}

std::optional<SourceRange> SourceFile::segmentRange(std::string_view segmentName) const noexcept {
  const auto* value = segment(segmentName);
  if (value == nullptr || value->offset > size || value->size > size - value->offset) {
    return std::nullopt;
  }
  return SourceRange{.source = id, .offset = value->offset, .size = value->size};
}

ByteReader::ByteReader(SourceId source, std::span<const u8> bytes) : source_(source), bytes_(bytes) {
}

RetainedSource::RetainedSource(SourceId source, SharedSourceBytes bytes) : source_(source), bytes_(std::move(bytes)) {
}

RetainedSource RetainedSource::copyOf(ByteReader reader) {
  const auto bytes = reader.slice(0, reader.size());
  return RetainedSource{reader.source(), std::make_shared<const std::vector<u8>>(bytes.begin(), bytes.end())};
}

ByteReader RetainedSource::reader() const noexcept {
  return ByteReader{source_, bytes_ ? std::span<const u8>{*bytes_} : std::span<const u8>{}};
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
  file.status = SourceStatus::Active;
  file.size = bytes.size();
  if (file.name.empty() && !file.path.empty()) {
    file.name = file.path.filename().string();
  }
  entries_.push_back(Entry{
      .file = std::move(file),
      .bytes = std::make_shared<const std::vector<u8>>(std::move(bytes)),
  });
  return id;
}

SourceId SourceStore::addDerived(SourceFile file, std::vector<u8> bytes, SourceId defaultParent) {
  const SourceId parent = file.origin && file.origin->source.valid() ? file.origin->source : defaultParent;
  if (!contains(parent)) {
    throw std::invalid_argument("Derived source parent is not present");
  }

  file.kind = SourceKind::Derived;
  file.parent = parent;
  return add(std::move(file), std::move(bytes));
}

std::vector<SourceId> SourceStore::removeFamily(SourceId id) {
  const auto family = sourceFamily(id);
  for (const SourceId source : family) {
    auto& entry = entries_[source.value];
    entry.file.status = SourceStatus::Removed;
    entry.file.size = 0;
    entry.bytes.reset();
  }
  return family;
}

bool SourceStore::contains(SourceId id) const noexcept {
  return hasSlot(id) && entries_[id.value].file.active();
}

bool SourceStore::hasSlot(SourceId id) const noexcept {
  return id.valid() && id.value < entries_.size();
}

std::span<const u8> SourceStore::bytes(SourceId id) const {
  const auto& e = entry(id);
  return *e.bytes;
}

SharedSourceBytes SourceStore::sharedBytes(SourceId id) const {
  return entry(id).bytes;
}

ByteReader SourceStore::reader(SourceId id) const {
  return ByteReader{id, bytes(id)};
}

const SourceFile& SourceStore::source(SourceId id) const {
  return entry(id).file;
}

const SourceFile& SourceStore::sourceAt(size_t index) const {
  size_t activeIndex = 0;
  for (const auto& entry : entries_) {
    if (!entry.file.active()) {
      continue;
    }
    if (activeIndex == index) {
      return entry.file;
    }
    ++activeIndex;
  }

  throw std::out_of_range("Source index outside SourceStore bounds");
}

size_t SourceStore::sourceCount() const noexcept {
  return static_cast<size_t>(std::ranges::count_if(entries_, [](const Entry& entry) { return entry.file.active(); }));
}

std::vector<SourceFile> SourceStore::sourceFiles() const {
  std::vector<SourceFile> files;
  files.reserve(sourceCount());
  for (const auto& e : entries_) {
    if (e.file.active()) {
      files.push_back(e.file);
    }
  }
  return files;
}

std::vector<SourceId> SourceStore::sourceFamily(SourceId id) const {
  std::vector<SourceId> family;
  if (!contains(id)) {
    return family;
  }

  family.push_back(id);
  for (size_t index = 0; index < family.size(); ++index) {
    const SourceId parent = family[index];
    for (const auto& entry : entries_) {
      if (entry.file.active() && entry.file.parent == parent) {
        family.push_back(entry.file.id);
      }
    }
  }
  return family;
}

std::vector<SourceId> SourceStore::activeUserSources() const {
  std::vector<SourceId> sources;
  for (const auto& entry : entries_) {
    if (entry.file.active() && !entry.file.derived()) {
      sources.push_back(entry.file.id);
    }
  }
  return sources;
}

const SourceStore::Entry& SourceStore::entry(SourceId id) const {
  if (!contains(id)) {
    throw std::out_of_range("SourceId is not present in SourceStore");
  }
  return entries_[id.value];
}

}  // namespace vgmtrans::core
