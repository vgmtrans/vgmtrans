/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatDefinition.h"

#include <filesystem>
#include <istream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::mame {

inline constexpr std::string_view kMameExtractorName = "MameRomSet";
inline constexpr std::string_view kMameGameAttribute = "mame.game";
inline constexpr std::string_view kMameFormatAttribute = "mame.format";
inline constexpr std::string_view kMameFormatVersionAttribute = "mame.format-version";

enum class RomLoadMethod {
  Append,
  AppendSwap16,
  Deinterlace,
  DeinterlacePairs,
};

enum class RomLoadOrder {
  Normal,
  Reverse,
};

struct RomGroupDefinition {
  std::string name;
  RomLoadMethod loadMethod = RomLoadMethod::Append;
  RomLoadOrder loadOrder = RomLoadOrder::Normal;
  std::string encryption;
  std::map<std::string, std::string, std::less<>> attributes;
  std::vector<std::string> members;
};

struct RomSetDefinition {
  std::string name;
  std::string format;
  std::string formatVersion;
  std::vector<RomGroupDefinition> groups;
};

// Immutable value loaded from mame_roms.json. Registration captures a copy, so
// sessions never depend on mutable global loader state or the JSON file's
// lifetime after setup.
class RomDatabase {
public:
  [[nodiscard]] static RomDatabase parse(std::istream& input);
  [[nodiscard]] static RomDatabase load(const std::filesystem::path& path);

  [[nodiscard]] const RomSetDefinition* find(std::string_view name) const noexcept;
  [[nodiscard]] size_t size() const noexcept { return sets_.size(); }
  [[nodiscard]] bool empty() const noexcept { return sets_.empty(); }

private:
  std::map<std::string, RomSetDefinition, std::less<>> sets_;
};

// Applies one ROM-region layout to already-read member values. Keeping this
// transformation independent of ZIP I/O makes the MAME semantics directly
// testable and useful to callers that obtain ROM members from another store.
[[nodiscard]] std::vector<u8> assembleRomGroup(const RomGroupDefinition& group, std::vector<std::vector<u8>> members);

[[nodiscard]] core::FormatDefinition mameRomSetExtractorDefinition(RomDatabase database);

}  // namespace vgmtrans::formats::mame
