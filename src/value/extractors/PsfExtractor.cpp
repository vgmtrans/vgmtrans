/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/PsfExtractor.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::psf {

using namespace core;

namespace {

constexpr u8 kNds2sfVersion = 0x24;
constexpr u8 kNcsfVersion = 0x25;
constexpr u8 kGsfVersion = 0x22;
constexpr u8 kPsf1Version = 0x01;
constexpr u8 kSsfVersion = 0x11;
constexpr u32 kGbaRomBase = 0x08000000;
constexpr size_t kPsf1DataOffset = 0x800;
constexpr size_t kPsf1LoadAddressOffset = 0x18;
constexpr int kMaxRecursion = 10;

struct PsfData {
  u8 version = 0;
  std::vector<u8> exe;
  std::map<std::string, std::string> tags;
};

struct Image {
  // PSF libraries overlay byte ranges into an executable image. start/end track the
  // address span represented by data.
  u32 start = 0;
  u32 end = 0;
  std::vector<u8> data;
};

[[nodiscard]] bool supportedVersion(u8 version) {
  return version == kPsf1Version || version == kSsfVersion || version == kGsfVersion || version == kNds2sfVersion ||
         version == kNcsfVersion;
}

[[nodiscard]] std::optional<size_t> dataOffsetForVersion(u8 version) {
  switch (version) {
    case kPsf1Version:
      return kPsf1DataOffset;
    case kSsfVersion:
      // SSF uses the ordinary PSF mini-header: a little-endian load address
      // followed immediately by bytes to overlay into Saturn sound RAM.
      return 0x04;
    case kGsfVersion:
      return 0x0c;
    case kNds2sfVersion:
      return 0x08;
    case kNcsfVersion:
      return 0x00;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] bool hasPsfSignature(std::span<const u8> bytes) {
  return bytes.size() >= 16 && bytes[0] == 'P' && bytes[1] == 'S' && bytes[2] == 'F' && supportedVersion(bytes[3]);
}

[[nodiscard]] u32 le32(std::span<const u8> bytes, size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4) {
    throw std::runtime_error("PSF file is truncated");
  }
  return static_cast<u32>(bytes[offset]) | (static_cast<u32>(bytes[offset + 1]) << 8) |
         (static_cast<u32>(bytes[offset + 2]) << 16) | (static_cast<u32>(bytes[offset + 3]) << 24);
}

[[nodiscard]] Diagnostic warning(std::string message, SourceRange range) {
  return Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range};
}

[[nodiscard]] std::vector<u8> inflateZlib(std::span<const u8> compressed) {
  z_stream stream{};
  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
  stream.avail_in = static_cast<uInt>(compressed.size());
  if (inflateInit(&stream) != Z_OK) {
    throw std::runtime_error("PSF executable zlib stream could not be initialized");
  }

  std::vector<u8> output;
  std::array<u8, 32768> buffer{};
  int status = Z_OK;
  while (status == Z_OK) {
    stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
    stream.avail_out = static_cast<uInt>(buffer.size());
    status = inflate(&stream, Z_NO_FLUSH);
    const auto produced = buffer.size() - stream.avail_out;
    output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(produced));
  }

  inflateEnd(&stream);
  if (status != Z_STREAM_END) {
    throw std::runtime_error("PSF executable zlib stream could not be decompressed");
  }
  return output;
}

void parseTags(PsfData& psf, std::span<const u8> bytes, size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 5 ||
      !std::equal(bytes.begin() + offset, bytes.begin() + offset + 5, std::string_view("[TAG]").begin())) {
    return;
  }

  size_t cursor = offset + 5;
  while (cursor < bytes.size()) {
    const size_t lineEnd =
        std::find(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.end(), u8{'\n'}) - bytes.begin();
    const size_t equals = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                    bytes.begin() + static_cast<std::ptrdiff_t>(lineEnd), u8{'='}) -
                          bytes.begin();
    if (equals < lineEnd) {
      size_t nameBegin = cursor;
      size_t nameEnd = equals;
      size_t valueBegin = equals + 1;
      size_t valueEnd = lineEnd;
      while (nameBegin < nameEnd && bytes[nameBegin] <= 0x20) {
        ++nameBegin;
      }
      while (nameEnd > nameBegin && bytes[nameEnd - 1] <= 0x20) {
        --nameEnd;
      }
      while (valueBegin < valueEnd && bytes[valueBegin] <= 0x20) {
        ++valueBegin;
      }
      while (valueEnd > valueBegin && bytes[valueEnd - 1] <= 0x20) {
        --valueEnd;
      }

      std::string name(reinterpret_cast<const char*>(bytes.data() + nameBegin), nameEnd - nameBegin);
      std::string value(reinterpret_cast<const char*>(bytes.data() + valueBegin), valueEnd - valueBegin);
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      auto [it, inserted] = psf.tags.emplace(std::move(name), std::move(value));
      if (!inserted) {
        it->second += "\n";
        it->second.append(reinterpret_cast<const char*>(bytes.data() + valueBegin), valueEnd - valueBegin);
      }
    }
    cursor = lineEnd == bytes.size() ? bytes.size() : lineEnd + 1;
  }
}

[[nodiscard]] PsfData parsePsf(std::span<const u8> bytes) {
  // PSF stores a compressed executable payload followed by text tags. The version byte
  // determines how to interpret the decompressed payload header.
  if (!hasPsfSignature(bytes)) {
    throw std::runtime_error("Unsupported or invalid PSF file");
  }

  const u8 version = bytes[3];
  const u32 reservedSize = le32(bytes, 4);
  const u32 exeSize = le32(bytes, 8);
  const u32 expectedCrc = le32(bytes, 12);
  if (reservedSize > bytes.size() || exeSize > bytes.size() ||
      static_cast<u64>(16) + reservedSize + exeSize > bytes.size()) {
    throw std::runtime_error("PSF header sizes are invalid");
  }

  const size_t exeOffset = 16 + reservedSize;
  PsfData psf{.version = version};
  if (exeSize != 0) {
    const auto compressed = bytes.subspan(exeOffset, exeSize);
    const uLong actualCrc = crc32(crc32(0L, nullptr, 0), reinterpret_cast<const Bytef*>(compressed.data()), exeSize);
    if (actualCrc != expectedCrc) {
      throw std::runtime_error("PSF executable CRC32 mismatch");
    }
    psf.exe = inflateZlib(compressed);
  }

  parseTags(psf, bytes, exeOffset + exeSize);
  return psf;
}

[[nodiscard]] std::optional<std::string> primaryLibName(const PsfData& psf) {
  if (auto it = psf.tags.find("_lib"); it != psf.tags.end()) {
    return it->second;
  }
  return std::nullopt;
}

void overlay(Image& image, u32 address, const u8* data, size_t size) {
  // Libraries can extend the image before or after previous payloads. Resize and zero-fill
  // so later overlays land at their emulated addresses.
  if (size == 0) {
    return;
  }
  if (image.data.empty()) {
    image.start = address;
    image.end = address + static_cast<u32>(size);
    image.data.assign(data, data + size);
    return;
  }

  const u32 newStart = std::min(image.start, address);
  const u32 newEnd = std::max(image.end, address + static_cast<u32>(size));
  if (newStart != image.start) {
    image.data.insert(image.data.begin(), image.start - newStart, 0);
    image.start = newStart;
  }
  if (newEnd > image.end) {
    image.data.resize(newEnd - image.start, 0);
    image.end = newEnd;
  }
  std::copy(data, data + size, image.data.begin() + (address - image.start));
}

[[nodiscard]] std::vector<u8> readFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("could not open PSF library file");
  }

  stream.seekg(0, std::ios::end);
  const auto size = stream.tellg();
  if (size < 0) {
    throw std::runtime_error("could not stat PSF library file");
  }
  stream.seekg(0, std::ios::beg);

  std::vector<u8> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!stream) {
    throw std::runtime_error("could not read PSF library file");
  }
  return bytes;
}

void overlayPsfExe(const PsfData& psf, Image& image) {
  // The first word of the decompressed executable gives the load address. The playable
  // data begins after a version-specific mini-header. PSF1 is the exception: it stores
  // a PS-X EXE header, with the load address at 0x18 and the payload at 0x800.
  if (psf.exe.empty()) {
    return;
  }
  const auto dataOffset = dataOffsetForVersion(psf.version);
  const size_t addressOffset = psf.version == kPsf1Version ? kPsf1LoadAddressOffset : psf.version == kGsfVersion ? 4 : 0;
  if (!dataOffset || psf.exe.size() < *dataOffset || psf.exe.size() < addressOffset + 4) {
    throw std::runtime_error("PSF executable header is invalid");
  }
  const u32 address = le32(psf.exe, addressOffset);
  size_t size = psf.exe.size() - *dataOffset;
  if (psf.version == kGsfVersion) {
    const u32 declaredSize = le32(psf.exe, 8);
    if (declaredSize > size) {
      throw std::runtime_error("GSF executable payload size is invalid");
    }
    size = declaredSize;
  }
  overlay(image, address, psf.exe.data() + *dataOffset, size);
}

void loadWithLibs(const PsfData& psf, const std::filesystem::path& basePath, Image& image,
                  std::vector<Diagnostic>& diagnostics, SourceRange range, int depth = 0);

void tryOpenLib(const std::filesystem::path& basePath, const std::string& libName, Image& image,
                std::vector<Diagnostic>& diagnostics, SourceRange range, int depth) {
  if (basePath.empty()) {
    diagnostics.push_back(warning("PSF library could not be resolved without a source path: " + libName, range));
    return;
  }

  const auto libPath = basePath / libName;
  try {
    const auto bytes = readFile(libPath);
    const auto libPsf = parsePsf(bytes);
    loadWithLibs(libPsf, libPath.parent_path(), image, diagnostics, range, depth + 1);
  } catch (const std::exception& ex) {
    diagnostics.push_back(warning("PSF library could not be loaded: " + libPath.string() + ": " + ex.what(), range));
  }
}

void loadWithLibs(const PsfData& psf, const std::filesystem::path& basePath, Image& image,
                  std::vector<Diagnostic>& diagnostics, SourceRange range, int depth) {
  // Load _lib first, overlay the current file, then load numbered libraries. This matches
  // common PSF dependency ordering where the track file patches a shared driver image.
  if (depth >= kMaxRecursion) {
    diagnostics.push_back(warning("PSF library recursion limit was reached", range));
    return;
  }

  if (const auto lib = primaryLibName(psf)) {
    tryOpenLib(basePath, *lib, image, diagnostics, range, depth);
  }

  overlayPsfExe(psf, image);

  for (int i = 2;; ++i) {
    const auto key = "_lib" + std::to_string(i);
    const auto found = psf.tags.find(key);
    if (found == psf.tags.end()) {
      break;
    }
    tryOpenLib(basePath, found->second, image, diagnostics, range, depth);
  }
}

[[nodiscard]] std::string sourceName(const SourceFile& source) {
  if (!source.name.empty()) {
    return source.name;
  }
  if (!source.path.empty()) {
    return source.path.filename().string();
  }
  return "PSF image";
}

[[nodiscard]] std::optional<u32> gsfSongIndex(const PsfData& psf) {
  if (psf.version != kGsfVersion || psf.exe.size() <= 0x0c || le32(psf.exe, 0) != kGbaRomBase ||
      le32(psf.exe, 8) != psf.exe.size() - 0x0c) {
    return std::nullopt;
  }
  const auto payload = std::span<const u8>(psf.exe).subspan(0x0c);
  if (payload.size() == 1) {
    return payload[0];
  }
  if (payload.size() == 2) {
    return static_cast<u32>(payload[0]) | (static_cast<u32>(payload[1]) << 8);
  }
  if (payload.size() == 4) {
    return le32(payload, 0);
  }
  return std::nullopt;
}

}  // namespace

[[nodiscard]] ScanResult scanPsf(const ScanInput& input) {
  // The scanner emits one derived executable image. Platform-specific format modules then
  // inspect that image exactly as if it came from a ROM/container file.
  const auto bytes = input.reader.slice(0, input.reader.size());
  if (!hasPsfSignature(bytes)) {
    return {};
  }

  ScanResult result;
  const auto range = input.reader.range(0, input.reader.size());
  const auto psf = parsePsf(bytes);

  Image image;
  const std::filesystem::path basePath =
      input.source.path.empty() ? std::filesystem::path{} : input.source.path.parent_path();
  loadWithLibs(psf, basePath, image, result.diagnostics, range);
  if (image.data.empty()) {
    result.diagnostics.push_back(warning("PSF file did not produce an executable image", range));
    return result;
  }

  SourceFile file{
      .name = sourceName(input.source),
      .path = input.source.path,
  };
  if (auto title = psf.tags.find("title"); title != psf.tags.end() && !title->second.empty()) {
    file.title = title->second;
  }
  if (const auto songIndex = gsfSongIndex(psf)) {
    file.attributes.emplace("mp2k.song-index", std::to_string(*songIndex));
  }
  if (psf.version == kGsfVersion) {
    file.attributes.emplace("container-format", "GSF");
  }
  result.extractedSources.push_back(ExtractedSource{
      .file = std::move(file),
      .bytes = std::move(image.data),
      .origin = range,
  });
  return result;
}

FormatDefinition psfExtractorDefinition() {
  return FormatDefinition{.module = {.name = "PSF", .scan = scanPsf}};
}

}  // namespace vgmtrans::formats::psf
