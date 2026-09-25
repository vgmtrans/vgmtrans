/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/PsxAdpcm.h"

#include "value/synth/SynthBuilder.h"

#include <fmt/format.h>

#include <iterator>

namespace vgmtrans::core {

namespace {

constexpr u8 kEndFlag = 1;
constexpr u8 kRepeatFlag = 2;
constexpr u8 kLoopStartFlag = 4;

}  // namespace

std::optional<Loop> PsxAdpcmStream::loopAt(u32 byteOffset) const {
  if (byteOffset >= encodedData.size) {
    return std::nullopt;
  }
  const u32 start = psxAdpcmDecodedOffset(byteOffset);
  const u32 frames = psxAdpcmDecodedFrames(static_cast<u32>(encodedData.size));
  return Loop{
      .enabled = loop.enabled,
      .start = start,
      .length = start < frames ? frames - start : 0,
  };
}

std::optional<PsxAdpcmStream> inspectPsxAdpcmStream(ByteReader reader, u32 offset, u32 endOffset) {
  if (offset >= endOffset || !reader.has(offset, 1)) {
    return std::nullopt;
  }
  u32 cursor = offset;
  std::optional<u32> loopStartBytes;
  bool loops = false;
  while (cursor <= endOffset && kPsxAdpcmBlockBytes <= endOffset - cursor && reader.has(cursor, kPsxAdpcmBlockBytes)) {
    const u8 flags = reader.u8At(cursor + 1);
    if ((flags & kLoopStartFlag) != 0) {
      loopStartBytes = cursor - offset;
    }
    cursor += kPsxAdpcmBlockBytes;
    if ((flags & kEndFlag) != 0) {
      loops = (flags & kRepeatFlag) != 0;
      break;
    }
  }

  const u32 encodedLength = cursor - offset;
  if (encodedLength == 0) {
    return std::nullopt;
  }
  return PsxAdpcmStream{
      .encodedData = reader.range(offset, encodedLength),
      .loop =
          Loop{
              .enabled = loops,
              .start = loopStartBytes ? psxAdpcmDecodedOffset(*loopStartBytes) : 0,
              .length = loopStartBytes ? psxAdpcmDecodedOffset(encodedLength - *loopStartBytes) : 0,
          },
  };
}

std::map<u32, PsxAdpcmStream> inspectPsxAdpcmStreams(ByteReader reader, u32 sampleBase, const std::set<u32>& offsets,
                                                     u32 endOffset) {
  std::map<u32, PsxAdpcmStream> streams;
  for (auto current = offsets.begin(); current != offsets.end(); ++current) {
    const auto next = std::next(current);
    const u32 boundary = next == offsets.end() ? endOffset : sampleBase + *next;
    if (const auto stream = inspectPsxAdpcmStream(reader, sampleBase + *current, boundary)) {
      streams.emplace(*current, *stream);
    }
  }
  return streams;
}

void addPsxAdpcmSamples(SamplePoolBuilder& samples, const std::map<u32, PsxAdpcmStream>& streams, u32 sampleRate,
                        SourceAnnotationId parent) {
  for (const auto& [offset, stream] : streams) {
    auto sample = samples.add(offset, Sample{
                                          .name = fmt::format("Sample {}", samples.size()),
                                          .codec = AudioCodec::PsxAdpcm,
                                          .encodedData = stream.encodedData,
                                          .sampleRate = sampleRate,
                                          .loop = stream.loop,
                                      });
    auto source = sample.source(sample.value().name, stream.encodedData, "psx-adpcm-sample");
    if (parent.valid()) {
      source.parent(parent);
    }
  }
}

}  // namespace vgmtrans::core
