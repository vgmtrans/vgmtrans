/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/synth/SynthModel.h"

#include <optional>

namespace vgmtrans::core {

inline constexpr u32 kPsxAdpcmBlockBytes = 16;
inline constexpr u32 kPsxAdpcmFramesPerBlock = 28;

struct PsxAdpcmStream {
  SourceRange encodedData;
  Loop loop;
};

[[nodiscard]] constexpr u32 psxAdpcmDecodedFrames(u32 encodedBytes) noexcept {
  return (encodedBytes / kPsxAdpcmBlockBytes) * kPsxAdpcmFramesPerBlock;
}

[[nodiscard]] constexpr u32 psxAdpcmDecodedOffset(u32 encodedOffset) noexcept {
  return (encodedOffset / kPsxAdpcmBlockBytes) * kPsxAdpcmFramesPerBlock;
}

// Walks whole ADPCM blocks until the stream end flag or the supplied boundary.
// Loop metadata is retained even when the stream does not enable looping.
[[nodiscard]] std::optional<PsxAdpcmStream> inspectPsxAdpcmStream(ByteReader reader, u32 offset, u32 endOffset);

}  // namespace vgmtrans::core
