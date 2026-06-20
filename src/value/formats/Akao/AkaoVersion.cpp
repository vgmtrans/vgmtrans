/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoVersion.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

[[nodiscard]] std::string lowerCopy(std::string text) {
  std::ranges::transform(text, text.begin(),
                         [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

[[nodiscard]] bool containsAny(std::string_view text, std::initializer_list<std::string_view> needles) {
  return std::ranges::any_of(needles, [text](std::string_view needle) { return text.find(needle) != text.npos; });
}

}  // namespace

std::string versionName(AkaoPs1Version version) {
  switch (version) {
    case AkaoPs1Version::Version1_0:
      return "1.0";
    case AkaoPs1Version::Version1_1:
      return "1.1";
    case AkaoPs1Version::Version1_2:
      return "1.2";
    case AkaoPs1Version::Version2:
      return "2";
    case AkaoPs1Version::Version3_0:
      return "3.0";
    case AkaoPs1Version::Version3_1:
      return "3.1";
    case AkaoPs1Version::Version3_2:
      return "3.2";
    case AkaoPs1Version::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string dialectId(AkaoPs1Version version) {
  return "akao-ps1-" + versionName(version);
}

bool isVersion3OrLater(AkaoPs1Version version) noexcept {
  return version >= AkaoPs1Version::Version3_0;
}

AkaoPs1Version determineVersionFromSource(const SourceFile& source) {
  std::string haystack = source.name;
  if (source.title) {
    haystack += " ";
    haystack += *source.title;
  }
  if (!source.path.empty()) {
    haystack += " ";
    haystack += source.path.string();
  }
  haystack = lowerCopy(std::move(haystack));

  if (containsAny(haystack, {"chocobo dungeon 2", "final fantasy viii", "final fantasy 8", "chocobo racing",
                             "saga frontier 2", "racing lagoon"})) {
    return AkaoPs1Version::Version3_1;
  }
  if (containsAny(haystack, {"legend of mana", "front mission 3", "chrono cross", "vagrant story",
                             "final fantasy ix", "final fantasy 9", "final fantasy origins"})) {
    return AkaoPs1Version::Version3_2;
  }
  if (containsAny(haystack, {"final fantasy vii", "final fantasy 7"})) {
    return AkaoPs1Version::Version1_0;
  }
  if (containsAny(haystack, {"saga frontier"})) {
    return AkaoPs1Version::Version1_1;
  }
  if (containsAny(haystack, {"front mission 2", "chocobo's mysterious dungeon"})) {
    return AkaoPs1Version::Version1_2;
  }
  if (containsAny(haystack, {"parasite eve"})) {
    return AkaoPs1Version::Version2;
  }
  if (containsAny(haystack, {"another mind"})) {
    return AkaoPs1Version::Version3_0;
  }
  return AkaoPs1Version::Unknown;
}

AkaoPs1Version guessSequenceVersion(ByteReader reader, u32 offset) {
  if (reader.has(offset + 0x2c, 4) && reader.le32(offset + 0x2c) == 0) {
    return AkaoPs1Version::Version3_2;
  }
  if (reader.has(offset + 0x1c, 4) && reader.le32(offset + 0x1c) == 0) {
    return AkaoPs1Version::Version2;
  }
  return AkaoPs1Version::Version1_1;
}

AkaoPs1Version guessSampleVersion(ByteReader reader, u32 offset) {
  if (reader.has(offset + 0x40, 4) && reader.le32(offset + 0x40) == 0) {
    if (!reader.has(offset + 0x1c, 4)) {
      return AkaoPs1Version::Unknown;
    }
    const u32 artCount = reader.le32(offset + 0x1c);
    if (!reader.has(offset, artCount * 0x10ull)) {
      return AkaoPs1Version::Unknown;
    }
    for (u32 i = 0; i < artCount; ++i) {
      if (reader.u8At(offset + i * 0x10 + 0x0b) != 0) {
        return AkaoPs1Version::Version3_0;
      }
    }
    return AkaoPs1Version::Version3_2;
  }
  if ((reader.has(offset + 0x18, 4) && reader.le32(offset + 0x18) != 0) ||
      (reader.has(offset + 0x1c, 4) && reader.le32(offset + 0x1c) != 0)) {
    return AkaoPs1Version::Version2;
  }
  return AkaoPs1Version::Version1_1;
}

u32 trackAllocationBitsOffset(AkaoPs1Version version) noexcept {
  return isVersion3OrLater(version) ? 0x20 : 0x10;
}

u32 trackHeaderOffset(AkaoPs1Version version) noexcept {
  if (isVersion3OrLater(version)) {
    return 0x40;
  }
  return version == AkaoPs1Version::Version2 ? 0x20 : 0x14;
}

u32 sequenceLengthForVersion(ByteReader reader, u32 offset, AkaoPs1Version version) {
  if (!reader.has(offset + 6, 2)) {
    return 0;
  }
  const u32 stored = reader.le16(offset + 6);
  return isVersion3OrLater(version) ? stored : stored + 0x10;
}

}  // namespace vgmtrans::formats::akao
