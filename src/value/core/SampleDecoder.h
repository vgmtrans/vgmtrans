/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/SynthModel.h"

#include <optional>
#include <span>
#include <vector>

namespace vgmtrans::core {

struct SampleDecoder {
  using Decode = std::optional<DecodedSample> (*)(const Sample& sample, std::span<const u8> sourceBytes);

  AudioCodec codec = AudioCodec::Unknown;
  Decode decode = nullptr;
};

class SampleDecoderRegistry {
 public:
  static SampleDecoderRegistry withDefaultDecoders();

  void add(SampleDecoder decoder);

  [[nodiscard]] std::optional<DecodedSample> decode(
      const Sample& sample,
      std::span<const u8> sourceBytes) const;

 private:
  std::vector<SampleDecoder> decoders_;
};

}  // namespace vgmtrans::core
