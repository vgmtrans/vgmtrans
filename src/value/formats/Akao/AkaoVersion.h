/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/Akao/AkaoTypes.h"

namespace vgmtrans::formats::akao {

[[nodiscard]] std::string versionName(AkaoPs1Version version);
[[nodiscard]] std::string dialectId(AkaoPs1Version version);
[[nodiscard]] AkaoPs1Version determineVersionFromSource(const core::SourceFile& source);
[[nodiscard]] AkaoPs1Version guessSequenceVersion(core::ByteReader reader, u32 offset);
[[nodiscard]] AkaoPs1Version guessSampleVersion(core::ByteReader reader, u32 offset);

inline constexpr u32 kAkaoSignature = 0x414B414F;
inline constexpr u32 kAkaoPpqn = 0x30;
inline constexpr u32 kAkaoMaxAnalysisCommands = 65536;
inline constexpr u32 kAkaoMaxTrackCommands = 262144;

struct AkaoProfile {
  AkaoPs1Version version = AkaoPs1Version::Unknown;

  [[nodiscard]] bool known() const noexcept { return version != AkaoPs1Version::Unknown; }
  [[nodiscard]] bool legacyFamily() const noexcept;
  [[nodiscard]] bool version3OrLater() const noexcept;
  [[nodiscard]] bool version32() const noexcept { return version == AkaoPs1Version::Version3_2; }

  [[nodiscard]] bool isSubEventPrefix(u8 status) const noexcept;
  [[nodiscard]] bool isNoteOpcode(u8 status) const noexcept;
  [[nodiscard]] bool noteHasInlineDuration(u8 status) const noexcept;
  [[nodiscard]] bool hasLegacySampleHeader() const noexcept;
  [[nodiscard]] bool hasCompactArtRows() const noexcept;

  [[nodiscard]] u32 directOperandBytes(u8 status) const noexcept;
  [[nodiscard]] u32 subOperandBytes(u8 sub) const noexcept;
  [[nodiscard]] u32 artRowSize() const noexcept;
  [[nodiscard]] u32 legacySampleEndingArtId(core::ByteReader reader, u32 offset) const;
  [[nodiscard]] u32 spuDestinationAddress(core::ByteReader reader, u32 sampleCollectionOffset) const;
  [[nodiscard]] u32 legacyDrumRegionBytes() const noexcept;
  [[nodiscard]] bool legacyDrumRowIsBlank(core::ByteReader reader, u32 rowOffset) const;
  [[nodiscard]] u32 relativeDestination(u32 operandOffset, s16 relative) const noexcept;
  [[nodiscard]] u32 trackAllocationBitsOffset() const noexcept;
  [[nodiscard]] u32 trackHeaderOffset() const noexcept;
  [[nodiscard]] u32 sequenceLength(core::ByteReader reader, u32 offset) const;
  [[nodiscard]] u32 tempoMicrosPerQuarter(u16 tempo) const;
};

[[nodiscard]] constexpr AkaoProfile akaoProfile(AkaoPs1Version version) noexcept {
  return AkaoProfile{.version = version};
}

}  // namespace vgmtrans::formats::akao
