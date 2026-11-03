/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/synth/SynthModel.h"

#include <map>
#include <optional>
#include <set>

namespace vgmtrans::core {

class SamplePoolBuilder;

inline constexpr u32 kPsxAdpcmBlockBytes = 16;
inline constexpr u32 kPsxAdpcmFramesPerBlock = 28;

struct PsxAdpcmStream {
  SourceRange encodedData;
  Loop loop;

  // Reposition the loop within this stream, preserving its enable flag.
  // byteOffset is relative to encodedData; out-of-range offsets return no loop.
  [[nodiscard]] std::optional<Loop> loopAt(u32 byteOffset) const;
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

// Offsets and returned keys are relative to sampleBase. Each stream is bounded
// by the next offset; endOffset is the absolute boundary for the final stream.
[[nodiscard]] std::map<u32, PsxAdpcmStream> inspectPsxAdpcmStreams(ByteReader reader, u32 sampleBase,
                                                                   const std::set<u32>& offsets, u32 endOffset);

// Adds annotated samples keyed by their relative offsets for samples.find().
void addPsxAdpcmSamples(SamplePoolBuilder& samples, const std::map<u32, PsxAdpcmStream>& streams, u32 sampleRate,
                        SourceAnnotationId parent = {});

}  // namespace vgmtrans::core
