/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/SampleDecoder.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

namespace {

s32 clipSigned15(s32 x) {
  return (x & 16384) ? (x | ~16383) : (x & 16383);
}

s32 clampSigned16(s32 x) {
  return std::clamp<s32>(x, -32768, 32767);
}

void decodeBrrBlock(std::span<s16, 16> output, u8 header, std::span<const u8, 8> payload,
                    s32& previous1, s32& previous2) {
  const auto range = static_cast<u8>((header & 0xf0) >> 4);
  const auto filter = static_cast<u8>((header & 0x0c) >> 2);
  const bool validHeader = range < 0x0d;

  s32 s1 = previous1;
  s32 s2 = previous2;

  for (size_t i = 0; i < payload.size(); ++i) {
    s8 sample1 = static_cast<s8>(payload[i]);
    s8 sample2 = static_cast<s8>(sample1 << 4);
    sample1 >>= 4;
    sample2 >>= 4;

    for (int nibble = 0; nibble < 2; ++nibble) {
      s32 out = nibble != 0 ? sample2 : sample1;
      out = validHeader ? ((out << range) >> 1) : (out & ~0x7ff);

      switch (filter) {
        case 1:
          out += s1 + ((-s1) >> 4);
          break;
        case 2:
          out += (s1 << 1) + ((-((s1 << 1) + s1)) >> 5) - s2 + (s2 >> 4);
          break;
        case 3:
          out += (s1 << 1) + ((-(s1 + (s1 << 2) + (s1 << 3))) >> 6) - s2 +
                 (((s2 << 1) + s2) >> 4);
          break;
        default:
          break;
      }

      out = clipSigned15(clampSigned16(out));
      s2 = s1;
      s1 = out;
      output[i * 2 + nibble] = static_cast<s16>(out << 1);
    }
  }

  previous1 = s1;
  previous2 = s2;
}

class SnesBrrDecoder final : public SampleDecoder {
 public:
  [[nodiscard]] AudioCodec codec() const noexcept override { return AudioCodec::SnesBrr; }

  [[nodiscard]] std::optional<DecodedSample> decode(
      const Sample& sample,
      std::span<const u8> sourceBytes) const override {
    const auto offset = sample.encodedData.offset;
    const auto size = sample.encodedData.size;
    if (offset > sourceBytes.size() || size > sourceBytes.size() - offset) {
      return std::nullopt;
    }

    const auto encoded = sourceBytes.subspan(offset, size);
    DecodedSample decoded{
        .sampleRate = sample.sampleRate,
        .channels = sample.channels,
        .loop = sample.loop,
    };

    s32 previous1 = 0;
    s32 previous2 = 0;
    for (size_t blockOffset = 0; blockOffset + 9 <= encoded.size(); blockOffset += 9) {
      const auto header = encoded[blockOffset];
      const auto payload = encoded.subspan(blockOffset + 1, 8);
      const auto outputOffset = decoded.pcm.size();
      decoded.pcm.resize(outputOffset + 16);
      decodeBrrBlock(std::span<s16, 16>(decoded.pcm.data() + outputOffset, 16),
                     header,
                     std::span<const u8, 8>(payload.data(), 8),
                     previous1,
                     previous2);

      if ((header & 0x01) != 0) {
        break;
      }
    }

    return decoded;
  }
};

}  // namespace

SampleDecoderRegistry SampleDecoderRegistry::withDefaultDecoders() {
  SampleDecoderRegistry registry;
  registry.add(std::make_unique<SnesBrrDecoder>());
  return registry;
}

void SampleDecoderRegistry::add(std::unique_ptr<SampleDecoder> decoder) {
  if (!decoder) {
    throw std::invalid_argument("Cannot register a null SampleDecoder");
  }
  decoders_.push_back(std::move(decoder));
}

std::optional<DecodedSample> SampleDecoderRegistry::decode(
    const Sample& sample,
    std::span<const u8> sourceBytes) const {
  const auto decoder = std::ranges::find_if(decoders_, [&sample](const auto& candidate) {
    return candidate->codec() == sample.codec;
  });
  if (decoder == decoders_.end()) {
    return std::nullopt;
  }
  return (*decoder)->decode(sample, sourceBytes);
}

}  // namespace vgmtrans::core
