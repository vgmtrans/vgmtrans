/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <optional>
#include <vector>
#include "Scanner.h"

class NinN64Scanner : public VGMScanner {
 public:
  explicit NinN64Scanner(Format *format) : VGMScanner(format) {}

  void scan(RawFile *file, void *info = nullptr) override;

 private:
  void searchForSequences(RawFile *file) const;
  [[nodiscard]] bool isPossibleSequenceHeader(const RawFile *file, size_t offset) const;
  [[nodiscard]] std::optional<std::vector<uint8_t>> decompressSequence(RawFile *file,
                                                                       uint32_t offset) const;
  [[nodiscard]] std::optional<std::vector<uint8_t>> decompressTrack(const RawFile *file,
                                                                    uint32_t trackStart) const;

  static constexpr size_t kMaxTracks = 16;
  static constexpr size_t kHeaderSize = 0x48;
};
