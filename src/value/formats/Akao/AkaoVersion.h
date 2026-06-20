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
[[nodiscard]] bool isVersion3OrLater(AkaoPs1Version version) noexcept;
[[nodiscard]] AkaoPs1Version determineVersionFromSource(const core::SourceFile& source);
[[nodiscard]] AkaoPs1Version guessSequenceVersion(core::ByteReader reader, u32 offset);
[[nodiscard]] AkaoPs1Version guessSampleVersion(core::ByteReader reader, u32 offset);
[[nodiscard]] u32 trackAllocationBitsOffset(AkaoPs1Version version) noexcept;
[[nodiscard]] u32 trackHeaderOffset(AkaoPs1Version version) noexcept;
[[nodiscard]] u32 sequenceLengthForVersion(core::ByteReader reader, u32 offset, AkaoPs1Version version);

}  // namespace vgmtrans::formats::akao
