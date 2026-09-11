/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/PsxAdpcm.h"

namespace vgmtrans::core {

namespace {

constexpr u8 kEndFlag = 1;
constexpr u8 kRepeatFlag = 2;
constexpr u8 kLoopStartFlag = 4;

}  // namespace

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

}  // namespace vgmtrans::core
