/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/MameRomSetExtractor.h"

#include <ioapi.h>
#include <nlohmann/json.hpp>
#include <unzip.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

namespace vgmtrans::formats::mame {

using namespace core;
using json = nlohmann::json;

namespace {

struct MemoryStream {
  std::span<const u8> bytes;
  u64 position = 0;
  bool error = false;
};

voidpf ZCALLBACK openMemory(voidpf opaque, const void*, int mode) {
  if ((mode & ZLIB_FILEFUNC_MODE_READ) == 0) {
    return nullptr;
  }
  auto* stream = static_cast<MemoryStream*>(opaque);
  stream->position = 0;
  stream->error = false;
  return stream;
}

uLong ZCALLBACK readMemory(voidpf, voidpf handle, void* destination, uLong requested) {
  auto* stream = static_cast<MemoryStream*>(handle);
  if (stream == nullptr || stream->position > stream->bytes.size()) {
    return 0;
  }
  const u64 available = stream->bytes.size() - stream->position;
  const auto count = static_cast<uLong>(std::min<u64>(available, requested));
  if (count != 0) {
    std::memcpy(destination, stream->bytes.data() + stream->position, count);
    stream->position += count;
  }
  return count;
}

uLong ZCALLBACK writeMemory(voidpf, voidpf, const void*, uLong) {
  return 0;
}

ZPOS64_T ZCALLBACK tellMemory(voidpf, voidpf handle) {
  const auto* stream = static_cast<const MemoryStream*>(handle);
  return stream != nullptr ? stream->position : 0;
}

long ZCALLBACK seekMemory(voidpf, voidpf handle, ZPOS64_T rawOffset, int origin) {
  auto* stream = static_cast<MemoryStream*>(handle);
  if (stream == nullptr) {
    return -1;
  }

  u64 position = 0;
  switch (origin) {
    case ZLIB_FILEFUNC_SEEK_SET:
      position = rawOffset;
      break;
    case ZLIB_FILEFUNC_SEEK_CUR:
      if (rawOffset > std::numeric_limits<u64>::max() - stream->position) {
        stream->error = true;
        return -1;
      }
      position = stream->position + rawOffset;
      break;
    case ZLIB_FILEFUNC_SEEK_END:
      if (rawOffset > stream->bytes.size()) {
        stream->error = true;
        return -1;
      }
      position = stream->bytes.size() - rawOffset;
      break;
    default:
      stream->error = true;
      return -1;
  }
  if (position > stream->bytes.size()) {
    stream->error = true;
    return -1;
  }
  stream->position = position;
  return 0;
}

int ZCALLBACK closeMemory(voidpf, voidpf) {
  return 0;
}

int ZCALLBACK errorMemory(voidpf, voidpf handle) {
  const auto* stream = static_cast<const MemoryStream*>(handle);
  return stream != nullptr && stream->error ? 1 : 0;
}

struct ZipCloser {
  void operator()(void* archive) const noexcept {
    if (archive != nullptr) {
      static_cast<void>(unzClose(archive));
    }
  }
};

using ZipPtr = std::unique_ptr<void, ZipCloser>;

[[nodiscard]] ZipPtr openZip(std::span<const u8> bytes, MemoryStream& stream) {
  stream.bytes = bytes;
  zlib_filefunc64_def functions{
      .zopen64_file = openMemory,
      .zread_file = readMemory,
      .zwrite_file = writeMemory,
      .ztell64_file = tellMemory,
      .zseek64_file = seekMemory,
      .zclose_file = closeMemory,
      .zerror_file = errorMemory,
      .opaque = &stream,
  };
  return ZipPtr{unzOpen2_64(&stream, &functions)};
}

[[nodiscard]] std::string scalarString(const json& value) {
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_null()) {
    return {};
  }
  if (value.is_boolean() || value.is_number()) {
    return value.dump();
  }
  throw std::runtime_error("MAME ROM attribute must be a scalar value");
}

[[nodiscard]] RomLoadMethod loadMethod(std::string_view value) {
  if (value == "append") {
    return RomLoadMethod::Append;
  }
  if (value == "append_swap16") {
    return RomLoadMethod::AppendSwap16;
  }
  if (value == "deinterlace") {
    return RomLoadMethod::Deinterlace;
  }
  if (value == "deinterlace_pairs") {
    return RomLoadMethod::DeinterlacePairs;
  }
  throw std::runtime_error("Unknown MAME ROM load_method: " + std::string(value));
}

[[nodiscard]] RomLoadOrder loadOrder(std::string_view value) {
  if (value.empty() || value == "normal") {
    return RomLoadOrder::Normal;
  }
  if (value == "reverse") {
    return RomLoadOrder::Reverse;
  }
  throw std::runtime_error("Unknown MAME ROM load_order: " + std::string(value));
}

[[nodiscard]] RomGroupDefinition parseGroup(const json& value) {
  if (!value.is_object() || !value.contains("type") || !value["type"].is_string() || !value.contains("load_method") ||
      !value["load_method"].is_string() || !value.contains("roms") || !value["roms"].is_array() ||
      value["roms"].empty()) {
    throw std::runtime_error("Invalid MAME ROM group definition");
  }

  RomGroupDefinition group{
      .name = value["type"].get<std::string>(),
      .loadMethod = loadMethod(value["load_method"].get<std::string>()),
  };
  if (const auto found = value.find("load_order"); found != value.end()) {
    if (!found->is_string()) {
      throw std::runtime_error("MAME ROM load_order must be a string");
    }
    group.loadOrder = loadOrder(found->get<std::string>());
  }
  if (const auto found = value.find("encryption"); found != value.end()) {
    if (!found->is_string()) {
      throw std::runtime_error("MAME ROM encryption must be a string");
    }
    group.encryption = found->get<std::string>();
  }
  if (const auto found = value.find("attributes"); found != value.end()) {
    if (!found->is_object()) {
      throw std::runtime_error("MAME ROM attributes must be an object");
    }
    for (const auto& [name, attribute] : found->items()) {
      group.attributes.emplace(name, scalarString(attribute));
    }
  }

  for (const auto& [name, attribute] : value.items()) {
    if (name == "type" || name == "load_method" || name == "load_order" || name == "roms" || name == "encryption" ||
        name == "attributes" || attribute.is_array() || attribute.is_object()) {
      continue;
    }
    group.attributes.insert_or_assign(name, scalarString(attribute));
  }

  for (const auto& member : value["roms"]) {
    if (!member.is_string()) {
      throw std::runtime_error("MAME ROM member name must be a string");
    }
    group.members.push_back(member.get<std::string>());
  }
  return group;
}

[[nodiscard]] RomSetDefinition parseSet(const json& value) {
  if (!value.is_object() || !value.contains("name") || !value["name"].is_string() || !value.contains("format") ||
      !value["format"].is_string() || !value.contains("rom_groups") || !value["rom_groups"].is_array()) {
    throw std::runtime_error("Invalid MAME ROM set definition");
  }

  RomSetDefinition set{
      .name = value["name"].get<std::string>(),
      .format = value["format"].get<std::string>(),
  };
  if (const auto found = value.find("fmt_version"); found != value.end()) {
    if (!found->is_string()) {
      throw std::runtime_error("MAME ROM fmt_version must be a string");
    }
    set.formatVersion = found->get<std::string>();
  }
  for (const auto& group : value["rom_groups"]) {
    set.groups.push_back(parseGroup(group));
  }
  if (set.groups.empty()) {
    throw std::runtime_error("MAME ROM set has no ROM groups: " + set.name);
  }
  return set;
}

[[nodiscard]] bool hasZipSignature(std::span<const u8> bytes) {
  return bytes.size() >= 4 && bytes[0] == 'P' && bytes[1] == 'K' &&
         ((bytes[2] == 3 && bytes[3] == 4) || (bytes[2] == 5 && bytes[3] == 6) || (bytes[2] == 7 && bytes[3] == 8));
}

[[nodiscard]] std::string archiveStem(const SourceFile& source) {
  const auto path = !source.path.empty() ? source.path : std::filesystem::path(source.name);
  return path.stem().string();
}

[[nodiscard]] std::vector<u8> readMember(unzFile archive, std::string_view name) {
  const std::string memberName(name);
  if (unzLocateFile(archive, memberName.c_str(), 0) != UNZ_OK) {
    throw std::runtime_error("missing ROM member '" + memberName + "'");
  }

  unz_file_info64 info{};
  if (unzGetCurrentFileInfo64(archive, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
      info.uncompressed_size > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error("could not read ROM member metadata for '" + memberName + "'");
  }
  if (unzOpenCurrentFile(archive) != UNZ_OK) {
    throw std::runtime_error("could not open ROM member '" + memberName + "'");
  }

  std::vector<u8> bytes(static_cast<size_t>(info.uncompressed_size));
  size_t position = 0;
  while (position < bytes.size()) {
    const auto request = static_cast<unsigned>(
        std::min<size_t>(bytes.size() - position, static_cast<size_t>(std::numeric_limits<unsigned>::max())));
    const int count = unzReadCurrentFile(archive, bytes.data() + position, request);
    if (count <= 0) {
      static_cast<void>(unzCloseCurrentFile(archive));
      throw std::runtime_error("could not decompress ROM member '" + memberName + "'");
    }
    position += static_cast<size_t>(count);
  }
  if (unzCloseCurrentFile(archive) != UNZ_OK) {
    throw std::runtime_error("could not finish ROM member '" + memberName + "'");
  }
  return bytes;
}

void appendBuffer(std::vector<u8>& output, std::span<const u8> bytes, bool swap16) {
  if (swap16 && (bytes.size() & 1u) != 0) {
    throw std::runtime_error("append_swap16 requires even-sized ROM members");
  }
  const size_t begin = output.size();
  output.resize(begin + bytes.size());
  if (!swap16) {
    std::ranges::copy(bytes, output.begin() + static_cast<std::ptrdiff_t>(begin));
    return;
  }
  for (size_t offset = 0; offset < bytes.size(); offset += 2) {
    output[begin + offset] = bytes[offset + 1];
    output[begin + offset + 1] = bytes[offset];
  }
}

[[nodiscard]] u32 hexAttribute(const RomGroupDefinition& group, std::string_view name) {
  const auto found = group.attributes.find(name);
  if (found == group.attributes.end()) {
    throw std::runtime_error("ROM group encryption is missing attribute '" + std::string(name) + "'");
  }
  std::string_view text = found->second;
  int base = 10;
  if (text.starts_with("0x") || text.starts_with("0X")) {
    text.remove_prefix(2);
    base = 16;
  }
  u32 value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (error != std::errc{} || end != text.data() + text.size()) {
    throw std::runtime_error("ROM group encryption attribute '" + std::string(name) + "' is not an integer");
  }
  return value;
}

[[nodiscard]] constexpr u8 kabukiBitswap1(u8 source, u32 key, u32 select) {
  u32 value = source;
  if ((select & (1u << ((key >> 0) & 7u))) != 0) {
    value = (value & 0xfcu) | ((value & 0x01u) << 1) | ((value & 0x02u) >> 1);
  }
  if ((select & (1u << ((key >> 4) & 7u))) != 0) {
    value = (value & 0xf3u) | ((value & 0x04u) << 1) | ((value & 0x08u) >> 1);
  }
  if ((select & (1u << ((key >> 8) & 7u))) != 0) {
    value = (value & 0xcfu) | ((value & 0x10u) << 1) | ((value & 0x20u) >> 1);
  }
  if ((select & (1u << ((key >> 12) & 7u))) != 0) {
    value = (value & 0x3fu) | ((value & 0x40u) << 1) | ((value & 0x80u) >> 1);
  }
  return static_cast<u8>(value);
}

[[nodiscard]] constexpr u8 kabukiBitswap2(u8 source, u32 key, u32 select) {
  u32 value = source;
  if ((select & (1u << ((key >> 12) & 7u))) != 0) {
    value = (value & 0xfcu) | ((value & 0x01u) << 1) | ((value & 0x02u) >> 1);
  }
  if ((select & (1u << ((key >> 8) & 7u))) != 0) {
    value = (value & 0xf3u) | ((value & 0x04u) << 1) | ((value & 0x08u) >> 1);
  }
  if ((select & (1u << ((key >> 4) & 7u))) != 0) {
    value = (value & 0xcfu) | ((value & 0x10u) << 1) | ((value & 0x20u) >> 1);
  }
  if ((select & (1u << ((key >> 0) & 7u))) != 0) {
    value = (value & 0x3fu) | ((value & 0x40u) << 1) | ((value & 0x80u) >> 1);
  }
  return static_cast<u8>(value);
}

[[nodiscard]] constexpr u8 rotateLeft1(u8 value) {
  return static_cast<u8>((value << 1) | (value >> 7));
}

[[nodiscard]] constexpr u8 kabukiByteDecode(u8 source, u32 swapKey1, u32 swapKey2, u32 xorKey, u32 select) {
  u8 value = kabukiBitswap1(source, swapKey1 & 0xffffu, select & 0xffu);
  value = rotateLeft1(value);
  value = kabukiBitswap2(value, swapKey1 >> 16, select & 0xffu);
  value ^= static_cast<u8>(xorKey);
  value = rotateLeft1(value);
  value = kabukiBitswap2(value, swapKey2 & 0xffffu, select >> 8);
  value = rotateLeft1(value);
  return kabukiBitswap1(value, swapKey2 >> 16, select >> 8);
}

void decryptKabukiData(std::vector<u8>& bytes, u32 swapKey1, u32 swapKey2, u32 addressKey, u32 xorKey) {
  constexpr size_t kEncryptedLength = 0x8000;
  if (bytes.size() < kEncryptedLength) {
    throw std::runtime_error("Kabuki encryption requires at least 0x8000 bytes");
  }
  for (u32 address = 0; address < kEncryptedLength; ++address) {
    const u32 select = (address ^ 0x1fc0u) + addressKey + 1;
    bytes[address] = kabukiByteDecode(bytes[address], swapKey1, swapKey2, xorKey, select);
  }
}

[[nodiscard]] constexpr u16 rotateLeft16(u16 value, u32 count) {
  return static_cast<u16>((value << count) | (value >> (16 - count)));
}

[[nodiscard]] constexpr u16 cps3RotateXor(u16 value, u16 xorValue) {
  u16 result = static_cast<u16>(value + rotateLeft16(value, 2));
  result = static_cast<u16>(rotateLeft16(result, 4) ^ (result & (value ^ xorValue)));
  return result;
}

[[nodiscard]] constexpr u32 cps3Mask(u32 address, u32 key1, u32 key2) {
  address ^= key1;
  u16 value = static_cast<u16>((address & 0xffff) ^ 0xffff);
  value = cps3RotateXor(value, static_cast<u16>(key2));
  value ^= static_cast<u16>((address >> 16) ^ 0xffff);
  value = cps3RotateXor(value, static_cast<u16>(key2 >> 16));
  value ^= static_cast<u16>(address) ^ static_cast<u16>(key2);
  return value | (static_cast<u32>(value) << 16);
}

void decryptCps3(std::vector<u8>& bytes, u32 key1, u32 key2) {
  if ((bytes.size() & 3u) != 0) {
    throw std::runtime_error("CPS3 encryption requires a ROM group whose size is divisible by four");
  }
  if (key1 == 0 || key2 == 0) {
    return;
  }
  for (size_t offset = 0; offset < bytes.size(); offset += 4) {
    const u32 encoded = (static_cast<u32>(bytes[offset]) << 24) | (static_cast<u32>(bytes[offset + 1]) << 16) |
                        (static_cast<u32>(bytes[offset + 2]) << 8) | bytes[offset + 3];
    const u32 decoded = encoded ^ cps3Mask(0x06000000 + static_cast<u32>(offset), key1, key2);
    bytes[offset] = static_cast<u8>(decoded >> 24);
    bytes[offset + 1] = static_cast<u8>(decoded >> 16);
    bytes[offset + 2] = static_cast<u8>(decoded >> 8);
    bytes[offset + 3] = static_cast<u8>(decoded);
  }
}

}  // namespace

std::vector<u8> assembleRomGroup(const RomGroupDefinition& group, std::vector<std::vector<u8>> buffers) {
  if (!group.encryption.empty() && group.encryption != "kabuki" && group.encryption != "cps3") {
    throw std::runtime_error("unsupported ROM group encryption '" + group.encryption + "'");
  }
  if (group.loadOrder == RomLoadOrder::Reverse && group.loadMethod != RomLoadMethod::DeinterlacePairs) {
    std::ranges::reverse(buffers);
  }

  std::vector<u8> output;
  size_t totalSize = 0;
  for (const auto& buffer : buffers) {
    if (buffer.size() > std::numeric_limits<size_t>::max() - totalSize) {
      throw std::runtime_error("assembled ROM group is too large");
    }
    totalSize += buffer.size();
  }
  output.reserve(totalSize);

  switch (group.loadMethod) {
    case RomLoadMethod::Append:
    case RomLoadMethod::AppendSwap16:
      for (const auto& buffer : buffers) {
        appendBuffer(output, buffer, group.loadMethod == RomLoadMethod::AppendSwap16);
      }
      break;

    case RomLoadMethod::Deinterlace: {
      if (!buffers.empty() &&
          !std::ranges::all_of(buffers, [&](const auto& buffer) { return buffer.size() == buffers.front().size(); })) {
        throw std::runtime_error("deinterlace requires equally sized ROM members");
      }
      for (size_t offset = 0; !buffers.empty() && offset < buffers.front().size(); ++offset) {
        for (const auto& buffer : buffers) {
          output.push_back(buffer[offset]);
        }
      }
      break;
    }

    case RomLoadMethod::DeinterlacePairs:
      if ((buffers.size() & 1u) != 0) {
        throw std::runtime_error("deinterlace_pairs requires an even number of ROM members");
      }
      for (size_t pair = 0; pair < buffers.size(); pair += 2) {
        if (buffers[pair].size() != buffers[pair + 1].size()) {
          throw std::runtime_error("deinterlace_pairs requires equally sized ROM pairs");
        }
        const auto& first = group.loadOrder == RomLoadOrder::Reverse ? buffers[pair + 1] : buffers[pair];
        const auto& second = group.loadOrder == RomLoadOrder::Reverse ? buffers[pair] : buffers[pair + 1];
        for (size_t offset = 0; offset < buffers[pair].size(); ++offset) {
          output.push_back(first[offset]);
          output.push_back(second[offset]);
        }
      }
      break;
  }
  if (group.encryption == "kabuki") {
    decryptKabukiData(output, hexAttribute(group, "kabuki_swap_key1"), hexAttribute(group, "kabuki_swap_key2"),
                      hexAttribute(group, "kabuki_addr_key"), hexAttribute(group, "kabuki_xor_key"));
  } else if (group.encryption == "cps3") {
    decryptCps3(output, hexAttribute(group, "key1"), hexAttribute(group, "key2"));
  }
  return output;
}

namespace {

[[nodiscard]] Diagnostic warning(std::string message, SourceRange range) {
  return Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range};
}

[[nodiscard]] ScanResult scanRomSet(const ScanInput& input, const RomDatabase& database) {
  const auto archiveBytes = input.reader.slice(0, input.reader.size());
  if (!hasZipSignature(archiveBytes)) {
    return {};
  }

  ScanResult result;
  const auto set = database.find(archiveStem(input.source));
  if (set == nullptr) {
    return result;
  }

  const SourceRange archiveRange = input.reader.range(0, input.reader.size());
  MemoryStream stream;
  auto archive = openZip(archiveBytes, stream);
  if (!archive) {
    result.diagnostics.push_back(warning("MAME ROM set archive could not be opened", archiveRange));
    return result;
  }

  SourceFile assembledFile{
      .name = set->name + " ROM regions",
      .title = set->name,
      .path = input.source.path,
      .attributes =
          {
              {std::string(kMameGameAttribute), set->name},
              {std::string(kMameFormatAttribute), set->format},
              {std::string(kMameFormatVersionAttribute), set->formatVersion},
          },
  };
  std::vector<u8> assembledBytes;
  try {
    for (const auto& group : set->groups) {
      std::vector<std::vector<u8>> buffers;
      buffers.reserve(group.members.size());
      for (const auto& member : group.members) {
        buffers.push_back(readMember(archive.get(), member));
      }
      auto bytes = assembleRomGroup(group, std::move(buffers));
      assembledFile.segments.push_back(SourceSegment{
          .name = group.name,
          .offset = assembledBytes.size(),
          .size = bytes.size(),
          .attributes = group.attributes,
      });
      assembledBytes.insert(assembledBytes.end(), std::make_move_iterator(bytes.begin()),
                            std::make_move_iterator(bytes.end()));
    }
  } catch (const std::exception& error) {
    result.diagnostics.push_back(
        warning("MAME ROM set '" + set->name + "' could not be assembled: " + error.what(), archiveRange));
    return result;
  }

  result.extractedSources.push_back(ExtractedSource{
      .file = std::move(assembledFile),
      .bytes = std::move(assembledBytes),
      .origin = archiveRange,
  });
  return result;
}

}  // namespace

RomDatabase RomDatabase::parse(std::istream& input) {
  json document;
  input >> document;
  const json* games = nullptr;
  if (document.is_array()) {
    games = &document;
  } else if (document.is_object()) {
    const auto found = document.find("games");
    if (found != document.end() && found->is_array()) {
      games = std::addressof(*found);
    }
  }
  if (games == nullptr) {
    throw std::runtime_error("MAME ROM database does not contain a games array");
  }

  RomDatabase database;
  for (const auto& value : *games) {
    auto set = parseSet(value);
    const std::string name = set.name;
    if (!database.sets_.emplace(name, std::move(set)).second) {
      throw std::runtime_error("Duplicate MAME ROM set definition: " + name);
    }
  }
  return database;
}

RomDatabase RomDatabase::load(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Could not open MAME ROM database: " + path.string());
  }
  return parse(input);
}

const RomSetDefinition* RomDatabase::find(std::string_view name) const noexcept {
  const auto found = sets_.find(name);
  return found != sets_.end() ? std::addressof(found->second) : nullptr;
}

FormatDefinition mameRomSetExtractorDefinition(RomDatabase database) {
  return FormatDefinition{
      .module =
          {
              .name = std::string(kMameExtractorName),
              .scan = [database = std::move(database)](const ScanInput& input) { return scanRomSet(input, database); },
          },
  };
}

}  // namespace vgmtrans::formats::mame
