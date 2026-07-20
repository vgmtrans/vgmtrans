/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

[[nodiscard]] std::string lowerCopy(std::string text) {
  std::ranges::transform(text, text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

[[nodiscard]] bool containsAny(std::string_view text, std::initializer_list<std::string_view> needles) {
  return std::ranges::any_of(needles, [text](std::string_view needle) { return text.find(needle) != text.npos; });
}

[[nodiscard]] double akaoTempoBpm(AkaoPs1Version version, u16 tempo) {
  if (tempo == 0) {
    return 1.0;
  }
  const u16 freq = version == AkaoPs1Version::Version1_0 ? 0x43d1 : 0x44e8;
  return 60.0 / (kAkaoPpqn * (65536.0 / tempo) * (freq / (33868800.0 / 8)));
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
  if (containsAny(haystack, {"legend of mana", "front mission 3", "chrono cross", "vagrant story", "final fantasy ix",
                             "final fantasy 9", "final fantasy origins"})) {
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
    const u32 articulationCount = reader.le32(offset + 0x1c);
    if (!reader.has(offset, articulationCount * 0x10ull)) {
      return AkaoPs1Version::Unknown;
    }
    for (u32 i = 0; i < articulationCount; ++i) {
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

bool AkaoProfile::legacyFamily() const noexcept {
  return version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1;
}

bool AkaoProfile::version3OrLater() const noexcept {
  return version >= AkaoPs1Version::Version3_0;
}

bool AkaoProfile::isSubEventPrefix(u8 status) const noexcept {
  return (version3OrLater() && status == 0xfe) ||
         ((version == AkaoPs1Version::Version1_2 || version == AkaoPs1Version::Version2) && status == 0xfc);
}

bool AkaoProfile::isNoteOpcode(u8 status) const noexcept {
  return status <= 0x99 || noteHasInlineDuration(status);
}

bool AkaoProfile::noteHasInlineDuration(u8 status) const noexcept {
  return version3OrLater() && status >= 0xf0 && status <= 0xfd;
}

u32 AkaoProfile::relativeDestination(u32 operandOffset, s16 relative) const noexcept {
  return static_cast<u32>(static_cast<s64>(operandOffset) + relative + (version3OrLater() ? 0 : 2));
}

u32 AkaoProfile::directOperandBytes(u8 status) const noexcept {
  switch (status) {
    case 0xa1:
    case 0xa2:
    case 0xa3:
    case 0xa5:
    case 0xa8:
    case 0xaa:
    case 0xac:
    case 0xad:
    case 0xae:
    case 0xaf:
    case 0xb1:
    case 0xb2:
    case 0xb5:
    case 0xb7:
    case 0xb9:
    case 0xbb:
    case 0xbd:
    case 0xbf:
    case 0xc0:
    case 0xc1:
    case 0xc9:
    case 0xce:
    case 0xcf:
    case 0xd2:
    case 0xd3:
    case 0xd8:
    case 0xd9:
    case 0xda:
    case 0xdc:
      return 1;
    case 0xa4:
    case 0xa9:
    case 0xab:
    case 0xb0:
    case 0xbc:
    case 0xdd:
    case 0xde:
    case 0xdf:
      return 2;
    case 0xb4:
    case 0xb8:
      return 3;
    case 0xe1:
      return version32() ? 1 : 0;
    case 0xe4:
    case 0xe5:
    case 0xe6:
      return version32() ? 2 : 0;
    case 0xe8:
    case 0xea:
    case 0xee:
      return legacyFamily() ? 2 : 0;
    case 0xe9:
    case 0xeb:
    case 0xef:
    case 0xf0:
    case 0xf1:
      return legacyFamily() ? 3 : 0;
    case 0xec:
      return legacyFamily() ? 2 : 0;
    case 0xf2:
      return legacyFamily() ? 1 : 0;
    case 0xf4:
    case 0xf7:
      return version == AkaoPs1Version::Version1_0 ? 2 : 0;
    case 0xf6:
      return version == AkaoPs1Version::Version1_0 ? 1 : 0;
    case 0xf8:
      return version == AkaoPs1Version::Version1_0 ? 1 : 0;
    case 0xfc:
      return version == AkaoPs1Version::Version1_1 ? 2 : 0;
    case 0xfd:
    case 0xfe:
      return legacyFamily() ? 2 : 0;
    default:
      return 0;
  }
}

u32 AkaoProfile::subOperandBytes(u8 sub) const noexcept {
  switch (sub) {
    case 0x00:
    case 0x02:
    case 0x14:
    case 0x15:
    case 0x16:
      return version3OrLater() ? (sub == 0x14 ? 1 : 2) : 2;
    case 0x01:
    case 0x03:
    case 0x07:
    case 0x08:
    case 0x09:
      return 3;
    case 0x04:
      return version3OrLater() ? 0 : 2;
    case 0x0a:
    case 0x0e:
    case 0x10:
    case 0x12:
    case 0x19:
    case 0x1c:
      if (sub == 0x0e && version32()) {
        return 2;
      }
      return (sub == 0x12 || sub == 0x19) ? 2 : 1;
    case 0x06:
    case 0x0c:
    case 0x0f:
    case 0x17:
    case 0x18:
      return sub == 0x0f && version32() ? 0 : 2;
    default:
      return 0;
  }
}

bool AkaoProfile::hasLegacySampleHeader() const noexcept {
  return !version3OrLater() && version >= AkaoPs1Version::Version1_1;
}

bool AkaoProfile::hasCompactArticulations() const noexcept {
  return version >= AkaoPs1Version::Version3_1;
}

u32 AkaoProfile::articulationSize() const noexcept {
  return hasCompactArticulations() ? 0x10 : 0x40;
}

u32 AkaoProfile::legacySampleEndingArticulationId(ByteReader reader, u32 offset) const {
  if (version == AkaoPs1Version::Version1_1) {
    return 0x80;
  }
  const u32 stored = reader.le32(offset + 0x1c);
  return stored == 0 ? 0x100 : stored;
}

u32 AkaoProfile::spuDestinationAddress(ByteReader reader, u32 sampleCollectionOffset) const {
  if (version == AkaoPs1Version::Version1_0) {
    return reader.has(sampleCollectionOffset, 4) ? reader.le32(sampleCollectionOffset) : 0;
  }
  return reader.has(sampleCollectionOffset + 0x10, 4) ? reader.le32(sampleCollectionOffset + 0x10) : 0;
}

u32 AkaoProfile::legacyDrumRegionBytes() const noexcept {
  return version >= AkaoPs1Version::Version2 ? 6 : 5;
}

bool AkaoProfile::legacyDrumRegionIsBlank(ByteReader reader, u32 regionOffset) const {
  return reader.le32(regionOffset) == 0 && reader.u8At(regionOffset + 4) == 0 &&
         (version < AkaoPs1Version::Version2 || reader.u8At(regionOffset + 5) == 0);
}

u32 AkaoProfile::trackAllocationBitsOffset() const noexcept {
  return version3OrLater() ? 0x20 : 0x10;
}

u32 AkaoProfile::trackHeaderOffset() const noexcept {
  if (version3OrLater()) {
    return 0x40;
  }
  return version == AkaoPs1Version::Version2 ? 0x20 : 0x14;
}

u32 AkaoProfile::sequenceLength(ByteReader reader, u32 offset) const {
  if (!reader.has(offset + 6, 2)) {
    return 0;
  }
  const u32 stored = reader.le16(offset + 6);
  return version3OrLater() ? stored : stored + 0x10;
}

double AkaoProfile::tempoBpm(u16 tempo) const {
  return akaoTempoBpm(version, tempo);
}

u32 AkaoProfile::tempoMicrosPerQuarter(u16 tempo) const {
  const double bpm = tempoBpm(tempo);
  return static_cast<u32>(std::clamp(std::llround(60000000.0 / std::max(1.0, bpm)), 1ll,
                                     static_cast<long long>(std::numeric_limits<u32>::max())));
}

}  // namespace vgmtrans::formats::akao
