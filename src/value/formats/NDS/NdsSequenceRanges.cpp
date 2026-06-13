/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequenceRanges.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr std::string_view kSseqSignature = "SSEQ\xff\xfe\x00\x01";

[[nodiscard]] bool matches(ByteReader reader, u64 offset, std::string_view signature) {
  if (!reader.has(offset, signature.size())) {
    return false;
  }
  for (size_t i = 0; i < signature.size(); ++i) {
    if (reader.u8At(offset + i) != static_cast<u8>(signature[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u32> nearbySseqHeader(ByteReader reader, u32 offset, u32 size) {
  constexpr u32 kMaxPaddingBeforeSseq = 0x200;
  const u64 searchEnd = std::min<u64>(reader.size(), static_cast<u64>(offset) + size + kMaxPaddingBeforeSseq);
  for (u64 candidate = offset + 1; candidate + kSseqSignature.size() <= searchEnd; ++candidate) {
    if (matches(reader, candidate, kSseqSignature)) {
      return static_cast<u32>(candidate);
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool isZeroFilled(ByteReader reader, u32 begin, u32 end) {
  for (u32 offset = begin; offset < end && reader.has(offset, 1); ++offset) {
    if (reader.u8At(offset) != 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool shouldRecoverMalformedSdatRange(ByteReader reader, u32 offset, u32 size) {
  const auto sseqOffset = nearbySseqHeader(reader, offset, size);
  if (!sseqOffset) {
    return false;
  }

  const u32 trackStart = offset + 0x1c;
  const u32 paddingEnd = std::min(*sseqOffset, offset + size);
  // Some zero-filled pseudo-sequences overlap a later SSEQ. If the padding
  // would align the SSEQ signature as bogus note data, leave it empty.
  if (size <= 0x100 && *sseqOffset >= trackStart && isZeroFilled(reader, offset, paddingEnd) &&
      ((*sseqOffset - trackStart) % 3) == 2) {
    return false;
  }
  return true;
}

}  // namespace

NdsSequenceRange ndsSequenceRangeForFatEntry(ByteReader reader, u32 offset, u32 size) {
  const bool hasSseqHeader = matches(reader, offset, kSseqSignature);
  const bool recoverMalformedSdatRange = !hasSseqHeader && shouldRecoverMalformedSdatRange(reader, offset, size);
  const bool extendRecoveryPastFatRange = recoverMalformedSdatRange && size <= 0x100;
  const u32 sequenceEnd = hasSseqHeader || !extendRecoveryPastFatRange
                              ? static_cast<u32>(std::min<u64>(reader.size(), static_cast<u64>(offset) + size))
                              : static_cast<u32>(reader.size());
  return NdsSequenceRange{
      .offset = offset,
      .size = size,
      .sequenceEnd = sequenceEnd,
      .recoverMalformedSdatRange = recoverMalformedSdatRange,
  };
}

}  // namespace vgmtrans::formats::nds
