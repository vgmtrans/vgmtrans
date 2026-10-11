/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OceanSnes/OceanSnes.h"
#include "value/scan/BytePattern.h"

namespace vgmtrans::formats::ocean_snes {
using namespace core;

namespace {

// The two supported builds have the same direct-page ABI. Match independent
// routines, including the revision-specific C2 handler, rather than song data
// or the mutable PRNG bytes embedded beside the command table.
constexpr auto kDispatch =
    makeMaskedBytePattern("\x80\xa8\xc0\x1c\xfd\xf6\x29\x0a\x2d\xf6\x28\x0a\x2d\x6f", "xxxxxx??xx??xx");
constexpr auto kHeader =
    makeMaskedBytePattern("\xfa\xe8\x30\xfa\xe9\x31\x8d\x00\xf7\x30\xc4\x34\xfc\xf7\x30\xc4\x35", "xxxxxxxxxxxxxxxxx");
constexpr auto kWaterworldSlide = makeMaskedBytePattern("\x3f\x07\x0c\x1c\xc4\x5c\x3f\xba\x18", "xxxxxxxxx");
constexpr auto kJp2Slide =
    makeMaskedBytePattern("\x3f\x04\x0c\x10\x09\x48\xff\xbc\x5c\x48\xff\xbc\x2f\x01\x5c", "xxxxxxxxxxxxxxx");

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto dispatch = findBytePattern(reader, kDispatch);
  const auto headerLoader = findBytePattern(reader, kHeader);
  if (!dispatch || !headerLoader || reader.u8At(0x640) != 0x8f || reader.le16(0x641) != 0xfaa2) {
    return std::nullopt;
  }
  Layout layout;
  const u16 commands = reader.le16(*dispatch + 10);
  if (*dispatch == 0xa1a && commands == 0xa28 && *headerLoader == 0xaaa &&
      findBytePattern(reader, kWaterworldSlide) == 0xfe0) {
    layout.version = Version::Waterworld;
    layout.pitchTable = 0xe6d;
    layout.sineTable = 0x18f3;
    layout.randomSeed = 0xa7c;
    layout.bendPattern = 0xa96;
  } else if (*dispatch == 0xa17 && commands == 0xa25 && *headerLoader == 0xaa7 &&
             findBytePattern(reader, kJp2Slide) == 0xfdd) {
    layout.version = Version::JurassicPark2;
    layout.pitchTable = 0xe6a;
    layout.sineTable = 0x18f0;
    layout.randomSeed = 0xa79;
    layout.bendPattern = 0xa93;
  } else {
    return std::nullopt;
  }
  if (reader.le16(*dispatch + 6) != commands + 1 ||
      reader.le16(commands + 4) != (layout.version == Version::Waterworld ? 0xfe0 : 0xfdd)) {
    return std::nullopt;
  }

  // The host can relocate the music bank. E8 is its header, not a live stream
  // cursor; start at authored order zero rather than the captured playback row.
  layout.header = reader.le16(0xe8);
  if (layout.header < 0x2000 || !reader.has(layout.header, 26)) {
    return std::nullopt;
  }
  layout.orders = reader.le16(layout.header);
  layout.instruments = reader.le16(layout.header + 2);
  layout.samples = reader.le16(layout.header + 4);
  if (layout.orders < 0x2000 || layout.instruments < 0x2000 || layout.samples < 0x2000 ||
      !reader.has(layout.instruments, 2) || !reader.has(layout.samples, 2)) {
    return std::nullopt;
  }
  for (u32 index = 0; index < 128 && reader.has(layout.orders + index * 2, 2); ++index) {
    const u16 block = reader.le16(layout.orders + index * 2);
    if ((block >> 8) == 0xff) {
      return layout.orderBlocks.empty() ? std::nullopt : std::optional{layout};
    }
    if (block < 0x2000 || !reader.has(block, 16)) {
      return std::nullopt;
    }
    for (u32 track = 0; track < 8; ++track) {
      const u16 start = reader.le16(block + track * 2);
      if (start < 0x2000 || start == 0xffff) {
        return std::nullopt;
      }
    }
    layout.orderBlocks.push_back(block);
  }
  return std::nullopt;
}

}  // namespace vgmtrans::formats::ocean_snes
