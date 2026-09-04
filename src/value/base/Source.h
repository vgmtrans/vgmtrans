/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Types.h"
#include "value/base/CoreTypes.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::core {

using SharedSourceBytes = std::shared_ptr<const std::vector<u8>>;

// Stable descriptions of source bytes whose representation is known before
// scanning. Unknown sources omit knownFormat and retain unrestricted probing.
namespace source_formats {
inline constexpr char kCps1[] = "CPS1";
inline constexpr char kCps2[] = "CPS2";
inline constexpr char kGbaRom[] = "GbaRom";
inline constexpr char kKonamiArcade[] = "KonamiArcade";
inline constexpr char kKonamiTMNT2[] = "KonamiTMNT2";
inline constexpr char kMameRomSet[] = "MameRomSet";
inline constexpr char kNintendoDsRom[] = "NintendoDsRom";
inline constexpr char kPlayStationRam[] = "PlayStationRam";
inline constexpr char kPsf[] = "Psf";
inline constexpr char kRsn[] = "Rsn";
inline constexpr char kSaturnRam[] = "SaturnRam";
inline constexpr char kSpc[] = "Spc";
inline constexpr char kSnesAram[] = "SnesAram";
}  // namespace source_formats

enum class SourceKind {
  UserLoaded,
  Derived,
};

enum class SourceStatus {
  Active,
  Removed,
};

// A named byte range inside a container-derived source. Extractors use segments
// to preserve the layout of assembled inputs (for example, MAME ROM regions)
// without inventing a format-specific wrapper file.
struct SourceSegment {
  std::string name;
  u64 offset = 0;
  u64 size = 0;
  std::map<std::string, std::string, std::less<>> attributes;

  [[nodiscard]] std::optional<std::string_view> attribute(std::string_view key) const noexcept;
};

struct SourceFile {
  SourceId id;
  SourceKind kind = SourceKind::UserLoaded;
  SourceStatus status = SourceStatus::Active;
  std::string name;
  std::optional<std::string> title;
  // Host filesystem location. Derived sources retain their outer container's
  // path; a future container member path should be modeled separately.
  std::filesystem::path path;
  u64 size = 0;
  // Derived sources are real session entries, such as archive members, SPC RAM,
  // or PSF executable images. parent/origin record where they came from.
  std::optional<SourceId> parent;
  std::optional<SourceRange> origin;
  // Authoritative knowledge about the representation of these bytes. Modules
  // advertise the known formats they accept; an absent value means discovery.
  std::optional<std::string> knownFormat;
  // Extractor-defined metadata and named regions remain ordinary values owned by
  // the source. Format modules can therefore consume container context without
  // loader pointers or process-global side channels.
  std::map<std::string, std::string, std::less<>> attributes;
  std::vector<SourceSegment> segments;

  [[nodiscard]] bool derived() const noexcept { return kind == SourceKind::Derived; }
  [[nodiscard]] bool active() const noexcept { return status == SourceStatus::Active; }
  [[nodiscard]] std::optional<std::string_view> attribute(std::string_view key) const noexcept;
  [[nodiscard]] const SourceSegment* segment(std::string_view segmentName) const noexcept;
  [[nodiscard]] std::optional<SourceRange> segmentRange(std::string_view segmentName) const noexcept;
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

  // ByteReader does not own bytes; its caller keeps the backing storage alive.
  SourceId source_;
  std::span<const u8> bytes_;
};

// Owns immutable source bytes for work that may outlive the scan that found it.
// ByteReader remains a cheap non-owning view; deferred runtimes retain this
// value explicitly and create readers only where they decode source data.
class RetainedSource {
public:
  RetainedSource() = default;
  RetainedSource(SourceId source, SharedSourceBytes bytes);

  [[nodiscard]] static RetainedSource copyOf(ByteReader reader);
  [[nodiscard]] ByteReader reader() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return bytes_ != nullptr; }

private:
  SourceId source_;
  SharedSourceBytes bytes_;
};

struct ExtractedSource {
  // Source extractors return bytes here. Session appends them as derived sources
  // so they can be inspected and processed like user-loaded files.
  SourceFile file;
  std::vector<u8> bytes;
};

class SourceStore {
public:
  // SourceStore owns all bytes referenced by SourceRange. Assets copy SourceRange
  // values instead of copying source bytes.
  SourceId add(SourceFile file, std::vector<u8> bytes);
  SourceId addDerived(SourceFile file, std::vector<u8> bytes, SourceId defaultParent);
  [[nodiscard]] std::vector<SourceId> removeFamily(SourceId id);

  [[nodiscard]] bool contains(SourceId id) const noexcept;
  [[nodiscard]] bool hasSlot(SourceId id) const noexcept;
  [[nodiscard]] std::span<const u8> bytes(SourceId id) const;
  [[nodiscard]] SharedSourceBytes sharedBytes(SourceId id) const;
  [[nodiscard]] ByteReader reader(SourceId id) const;
  [[nodiscard]] const SourceFile& source(SourceId id) const;
  [[nodiscard]] const SourceFile& sourceAt(size_t index) const;
  [[nodiscard]] size_t sourceCount() const noexcept;
  [[nodiscard]] std::vector<SourceFile> sourceFiles() const;
  [[nodiscard]] std::vector<SourceId> sourceFamily(SourceId id) const;
  [[nodiscard]] std::vector<SourceId> activeUserSources() const;

private:
  struct Entry {
    SourceFile file;
    SharedSourceBytes bytes;
  };

  [[nodiscard]] const Entry& entry(SourceId id) const;

  std::vector<Entry> entries_;
};

}  // namespace vgmtrans::core
