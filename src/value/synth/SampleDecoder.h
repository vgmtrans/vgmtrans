/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/synth/SynthModel.h"

#include <optional>
#include <span>
#include <vector>

namespace vgmtrans::core {

struct SampleDecoder {
  // Decode functions receive the whole source span and the Sample's encodedData range.
  // That lets them validate ranges and share code for samples packed into one source file.
  using Decode = std::optional<DecodedSample> (*)(const Sample& sample, std::span<const u8> sourceBytes);

  AudioCodec codec = AudioCodec::Unknown;
  Decode decode = nullptr;
};

class SampleDecoderRegistry {
public:
  // Covers codecs used by the current formats. Tests or future formats can build
  // their own registry if they need different decoder functions.
  static SampleDecoderRegistry withDefaultDecoders();

  void add(SampleDecoder decoder);

  [[nodiscard]] std::optional<DecodedSample> decode(const Sample& sample, std::span<const u8> sourceBytes) const;

private:
  std::vector<SampleDecoder> decoders_;
};

}  // namespace vgmtrans::core
