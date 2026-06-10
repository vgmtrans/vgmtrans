/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/SynthModel.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace vgmtrans::core {

class SampleDecoder {
 public:
  virtual ~SampleDecoder() = default;

  [[nodiscard]] virtual AudioCodec codec() const noexcept = 0;
  [[nodiscard]] virtual std::optional<DecodedSample> decode(
      const Sample& sample,
      std::span<const u8> sourceBytes) const = 0;
};

class SampleDecoderRegistry {
 public:
  static SampleDecoderRegistry withDefaultDecoders();

  void add(std::unique_ptr<SampleDecoder> decoder);

  [[nodiscard]] std::optional<DecodedSample> decode(
      const Sample& sample,
      std::span<const u8> sourceBytes) const;

 private:
  std::vector<std::unique_ptr<SampleDecoder>> decoders_;
};

}  // namespace vgmtrans::core
