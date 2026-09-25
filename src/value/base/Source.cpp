/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/base/Source.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

namespace {

std::filesystem::path fileIdentity(const std::filesystem::path& path, bool member) {
  if (member) {
    auto name = path.generic_string();
    std::ranges::replace(name, '\\', '/');
    return std::filesystem::path(name).lexically_normal();
  }
  return path.empty() ? path : std::filesystem::weakly_canonical(std::filesystem::absolute(path));
}

bool samePathScope(const SourceFile* left, const SourceFile* right) {
  return left != nullptr && right != nullptr && left->memberPath.has_value() == right->memberPath.has_value() &&
         (!left->memberPath || sameContainer(left, right));
}

}  // namespace

std::vector<u8> readFileBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("failed to open source file: " + path.string());
  }

  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if (size < 0) {
    throw std::runtime_error("failed to stat source file: " + path.string());
  }
  file.seekg(0, std::ios::beg);

  std::vector<u8> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!file) {
    throw std::runtime_error("failed to read source file: " + path.string());
  }
  return bytes;
}

std::optional<std::string_view> SourceSegment::attribute(std::string_view key) const noexcept {
  const auto found = attributes.find(key);
  return found != attributes.end() ? std::optional<std::string_view>{found->second} : std::nullopt;
}

std::optional<std::string_view> SourceFile::attribute(std::string_view key) const noexcept {
  const auto found = attributes.find(key);
  return found != attributes.end() ? std::optional<std::string_view>{found->second} : std::nullopt;
}

std::filesystem::path SourceFile::logicalPath() const {
  return memberPath ? fileIdentity(*memberPath, true)
                    : (path.empty() ? std::filesystem::path(name) : path).lexically_normal();
}

std::filesystem::path sourcePath(const SourceFile* source) {
  return source != nullptr ? source->logicalPath() : std::filesystem::path{};
}

std::filesystem::path sourceDirectory(const SourceFile* source) {
  return sourcePath(source).parent_path();
}

bool sameContainer(const SourceFile* left, const SourceFile* right) noexcept {
  return left != nullptr && right != nullptr && left->parent && left->parent == right->parent;
}

bool sameDirectory(const SourceFile* left, const SourceFile* right) {
  const auto a = sourcePath(left);
  const auto b = sourcePath(right);
  return samePathScope(left, right) && !a.empty() && !b.empty() && a.parent_path() == b.parent_path();
}

bool sameStem(const SourceFile* left, const SourceFile* right) {
  const auto a = sourcePath(left);
  const auto b = sourcePath(right);
  return samePathScope(left, right) && !a.empty() && !b.empty() && a.stem() == b.stem();
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

u32 ByteReader::le24(u64 offset) const {
  require(offset, 3);
  return static_cast<u32>(bytes_[offset]) | (static_cast<u32>(bytes_[offset + 1]) << 8) |
         (static_cast<u32>(bytes_[offset + 2]) << 16);
}

u32 ByteReader::be24(u64 offset) const {
  require(offset, 3);
  return (static_cast<u32>(bytes_[offset]) << 16) | (static_cast<u32>(bytes_[offset + 1]) << 8) |
         static_cast<u32>(bytes_[offset + 2]);
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

std::optional<u32> ByteReader::varLen(u32& offset, u32 end, u32 maxBytes) const noexcept {
  const u64 limit = std::min<u64>(end, bytes_.size());
  u32 value = 0;
  for (u32 count = 0; count < maxBytes && offset < limit; ++count) {
    const u8 byte = bytes_[offset++];
    value = (value << 7) | (byte & 0x7f);
    if ((byte & 0x80) == 0) {
      return value;
    }
  }
  return std::nullopt;
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
  if (file.parent && !contains(*file.parent)) {
    throw std::invalid_argument("Derived source parent is not present");
  }
  const auto path = file.derived() ? file.memberPath.value_or(std::filesystem::path{}) : file.path;
  if (const auto existing = findFile(path, file.parent)) {
    return *existing;
  }
  const auto identity = fileIdentity(path, file.parent.has_value());
  if (file.memberPath) {
    file.memberPath = identity;
  }
  const auto id = SourceId{static_cast<u32>(entries_.size())};
  file.id = id;
  file.size = bytes.size();
  if (file.name.empty() && !file.path.empty()) {
    file.name = file.path.filename().string();
  }
  entries_.push_back(Entry{
      .file = std::move(file),
      .bytes = std::make_shared<const std::vector<u8>>(std::move(bytes)),
  });
  if (!identity.empty()) {
    const auto parent = entries_.back().file.parent;
    auto& files = parent ? entries_[parent->value].members : fileIds_;
    files.insert_or_assign(identity, id);
  }
  return id;
}

std::optional<SourceId> SourceStore::findFile(const std::filesystem::path& path,
                                            std::optional<SourceId> parent) const {
  if (parent && !contains(*parent)) {
    return std::nullopt;
  }
  const auto& files = parent ? entry(*parent).members : fileIds_;
  const auto found = files.find(fileIdentity(path, parent.has_value()));
  return found != files.end() && contains(found->second) ? std::optional{found->second} : std::nullopt;
}

SourceId SourceStore::addDerived(SourceFile file, std::vector<u8> bytes, SourceId defaultParent) {
  file.parent = file.origin && file.origin->source.valid() ? file.origin->source : defaultParent;
  return add(std::move(file), std::move(bytes));
}

std::vector<SourceId> SourceStore::removeFamily(SourceId id) {
  const auto family = sourceFamily(id);
  for (const SourceId source : family) {
    auto& entry = entries_[source.value];
    entry.members.clear();
    entry.file.size = 0;
    entry.bytes.reset();
  }
  return family;
}

bool SourceStore::contains(SourceId id) const noexcept {
  return hasSlot(id) && entries_[id.value].bytes != nullptr;
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

size_t SourceStore::sourceCount() const noexcept {
  return static_cast<size_t>(std::ranges::count_if(entries_, [](const Entry& entry) { return entry.bytes != nullptr; }));
}

std::vector<SourceFile> SourceStore::sourceFiles() const {
  std::vector<SourceFile> files;
  files.reserve(sourceCount());
  for (const auto& e : entries_) {
    if (e.bytes) {
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
      if (entry.bytes && entry.file.parent == parent) {
        family.push_back(entry.file.id);
      }
    }
  }
  return family;
}

std::vector<SourceId> SourceStore::activeUserSources() const {
  std::vector<SourceId> sources;
  for (const auto& entry : entries_) {
    if (entry.bytes && !entry.file.derived()) {
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
