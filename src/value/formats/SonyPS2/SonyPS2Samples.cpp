/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS2/SonyPS2.h"

#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"

#include <fmt/format.h>

#include <algorithm>
#include <limits>

namespace vgmtrans::formats::sony_ps2 {

using namespace core;

namespace {

[[nodiscard]] bool zeroBlock(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kPsxAdpcmBlockBytes)) {
    return false;
  }
  for (u32 byte = 0; byte < kPsxAdpcmBlockBytes; ++byte) {
    if (reader.u8At(offset + byte) != 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool explicitPaddingBlock(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kPsxAdpcmBlockBytes)) {
    return false;
  }
  if (reader.u8At(offset) != 0 || reader.u8At(offset + 1) != 7) {
    return false;
  }
  for (u32 byte = 2; byte < kPsxAdpcmBlockBytes; ++byte) {
    if (reader.u8At(offset + byte) != 0x77) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool plausibleBlock(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kPsxAdpcmBlockBytes)) {
    return false;
  }
  const u8 filterShift = reader.u8At(offset);
  return (filterShift >> 4) <= 4 && (filterShift & 0x0f) <= 12 && (reader.u8At(offset + 1) & 0xf8) == 0;
}

}  // namespace

bool addSampleBody(ScanResultBuilder& result) {
  const ByteReader reader = result.reader();
  if (reader.size() < kPsxAdpcmBlockBytes || reader.size() > std::numeric_limits<u32>::max()) {
    return false;
  }
  const u32 end = static_cast<u32>(reader.size());
  u32 cursor = 0;
  SampleBodyData retained{.bytes = end};
  struct Parsed {
    u32 offset;
    std::optional<u32> zeroPrefix;
    PsxAdpcmStream stream;
  };
  std::vector<Parsed> parsed;
  while (cursor + kPsxAdpcmBlockBytes <= end) {
    while (cursor + kPsxAdpcmBlockBytes <= end && explicitPaddingBlock(reader, cursor)) {
      cursor += kPsxAdpcmBlockBytes;
    }
    std::optional<u32> zeroPrefix;
    if (cursor + kPsxAdpcmBlockBytes * 2 <= end && zeroBlock(reader, cursor) &&
        plausibleBlock(reader, cursor + kPsxAdpcmBlockBytes) && !zeroBlock(reader, cursor + kPsxAdpcmBlockBytes)) {
      zeroPrefix = cursor;
      cursor += kPsxAdpcmBlockBytes;
    }
    if (cursor == end) {
      break;
    }
    const u32 sampleOffset = cursor;
    bool foundEnd = false;
    while (cursor + kPsxAdpcmBlockBytes <= end && plausibleBlock(reader, cursor)) {
      const u8 flags = reader.u8At(cursor + 1);
      cursor += kPsxAdpcmBlockBytes;
      if ((flags & 1) != 0) {
        foundEnd = true;
        break;
      }
    }
    if (!foundEnd) {
      return false;
    }
    const auto stream = inspectPsxAdpcmStream(reader, sampleOffset, cursor);
    if (!stream || stream->encodedData.size != cursor - sampleOffset) {
      return false;
    }
    parsed.push_back(Parsed{sampleOffset, zeroPrefix, *stream});
  }
  if (parsed.empty() || cursor != end) {
    return false;
  }

  auto pool = result.samplePool(fmt::format("{} BD", result.sourceDisplayName()), reader.range(0, end));
  auto& samples = pool.samples();
  samples.include(reader.range(0, end));
  for (const auto& item : parsed) {
    const u32 denseIndex = static_cast<u32>(samples.size());
    auto entry = samples.add(item.offset, Sample{
                                              .name = fmt::format("VAG at {:#x}", item.offset),
                                              .codec = AudioCodec::PsxAdpcm,
                                              .encodedData = item.stream.encodedData,
                                              .sampleRate = kPs2SpuSampleRate,
                                              .channels = 1,
                                              .bitsPerSample = 16,
                                              .loop = item.stream.loop,
                                          });
    entry.source(entry.value().name, item.stream.encodedData, "sony-ps2-vag-stream");
    retained.entries.push_back(SampleBodyData::Entry{.bodyOffset = item.offset, .sampleIndex = denseIndex});
    if (item.zeroPrefix) {
      retained.entries.push_back(SampleBodyData::Entry{.bodyOffset = *item.zeroPrefix, .sampleIndex = denseIndex});
    }
  }
  pool.data(std::move(retained));
  return true;
}

}  // namespace vgmtrans::formats::sony_ps2
