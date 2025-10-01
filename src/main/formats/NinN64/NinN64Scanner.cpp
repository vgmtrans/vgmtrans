/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NinN64Scanner.h"
#include "NinN64Seq.h"
#include "Root.h"
#include "ScannerManager.h"
#include "RawFile.h"

namespace vgmtrans::scanners {
ScannerRegistration<NinN64Scanner> s_ninn64("NinN64");
}

void NinN64Scanner::scan(RawFile *file, void * /*info*/) {
  if (file == nullptr) {
    return;
  }

  searchForSequences(file);
}

void NinN64Scanner::searchForSequences(RawFile *file) const {
  const size_t fileSize = file->size();
  if (fileSize < kHeaderSize) {
    return;
  }

  for (size_t offset = 0; offset + kHeaderSize <= fileSize; ++offset) {
    if (!isPossibleSequenceHeader(file, offset)) {
      continue;
    }

    L_DEBUG("Potential Nin N64 sequence at 0x{:08X} in {}", static_cast<unsigned>(offset), file->name());

    auto decompressed = decompressSequence(file, static_cast<uint32_t>(offset));
    if (!decompressed) {
      L_DEBUG("Failed to decompress Nin N64 sequence at 0x{:08X}", static_cast<unsigned>(offset));
      continue;
    }

    auto seqDisplayName = fmt::format("{} @ {:06X}", file->name(), static_cast<unsigned>(offset));
    auto virtFileName = fmt::format("{} @ {:06X} (expanded)", file->name(), static_cast<unsigned>(offset));

    auto *newVirtFile =
        new VirtFile(decompressed->data(), static_cast<uint32_t>(decompressed->size()), virtFileName,
                     file->path().string(), file->tag);

    auto *seq = new NinN64Seq(newVirtFile, 0, seqDisplayName);
    bool loaded = seq->loadVGMFile();

    newVirtFile->setUseLoaders(false);
    newVirtFile->setUseScanners(false);
    if (pRoot != nullptr) {
      pRoot->setupNewRawFile(newVirtFile);
    }

    if (!loaded) {
      delete seq;
    }
  }
}

bool NinN64Scanner::isPossibleSequenceHeader(const RawFile *file, size_t offset) const {
  if (file == nullptr || offset + kHeaderSize > file->size()) {
    return false;
  }

  auto readByte = [file](size_t pos) { return file->readByte(pos); };

  if (readByte(offset) != 0 || readByte(offset + 1) != 0 || readByte(offset + 2) != 0 || readByte(offset + 3) != 0x44) {
    return false;
  }

  if (readByte(offset + 4) != 0 || readByte(offset + 5) >= 0x10) {
    return false;
  }

  if (readByte(offset + 8) != 0 || readByte(offset + 12) != 0) {
    return false;
  }

  if (readByte(offset + 0x40) != 0 || readByte(offset + 0x41) >= 3) {
    return false;
  }

  if (readByte(offset + 0x44) == 0 && readByte(offset + 0x45) == 0) {
    return false;
  }

  if (file->readWordBE(offset + 0x40) == 0) {
    return false;
  }

  uint32_t highestValue = 0;
  for (uint32_t k = 0; k < 15; k++) {
    const uint32_t first = file->readWordBE(offset + k * 4);
    const uint32_t second = file->readWordBE(offset + 4 + k * 4);

    if (second != 0) {
      if (second <= highestValue + 4) {
        return false;
      }
      highestValue = second;
    }

    if ((second != 0 && second < first) || second > 0x80000) {
      return false;
    }
  }

  return true;
}

std::optional<std::vector<uint8_t>> NinN64Scanner::decompressSequence(RawFile *file, uint32_t offset) const {
  if (file == nullptr || !file->isValidOffset(offset + kHeaderSize - 1)) {
    return std::nullopt;
  }

  std::array<uint32_t, kMaxTracks> originalPointers{};
  for (size_t i = 0; i < kMaxTracks; ++i) {
    originalPointers[i] = file->readWordBE(offset + static_cast<uint32_t>(i * 4));
  }

  std::vector<uint8_t> result(kHeaderSize);
  file->readBytes(offset, static_cast<uint32_t>(kHeaderSize), result.data());

  std::array<uint32_t, kMaxTracks> newPointers{};
  size_t writeOffset = kHeaderSize;

  for (size_t trackIndex = 0; trackIndex < kMaxTracks; ++trackIndex) {
    uint32_t relative = originalPointers[trackIndex];
    if (relative == 0) {
      continue;
    }

    uint32_t trackStart = offset + relative;
    if (!file->isValidOffset(trackStart)) {
      L_WARN("NinN64 compressed track pointer {} points outside file", trackIndex);
      return std::nullopt;
    }

    auto trackData = decompressTrack(file, trackStart);
    if (!trackData) {
      L_WARN("Failed to decompress NinN64 track {} at 0x{:08X}", trackIndex, trackStart);
      return std::nullopt;
    }

    newPointers[trackIndex] = static_cast<uint32_t>(writeOffset);
    result.insert(result.end(), trackData->begin(), trackData->end());
    writeOffset += trackData->size();
  }

  for (size_t i = 0; i < kMaxTracks; ++i) {
    uint32_t value = newPointers[i];
    result[i * 4 + 0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    result[i * 4 + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    result[i * 4 + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    result[i * 4 + 3] = static_cast<uint8_t>(value & 0xFF);
  }

  return result;
}

std::optional<std::vector<uint8_t>> NinN64Scanner::decompressTrack(const RawFile *file, uint32_t trackStart) const {
  if (file == nullptr || !file->isValidOffset(trackStart)) {
    return std::nullopt;
  }

  struct LoopState {
    uint32_t bytesRemaining;
    uint32_t returnOffset;
  };

  constexpr size_t kMaxTrackLength = 0x80000;

  std::vector<uint8_t> trackData;
  trackData.reserve(0x200);

  std::vector<LoopState> loopStack;
  uint32_t cur = trackStart;
  uint8_t runningStatus = 0;

  auto readNextByte = [&](uint8_t &out) -> bool {
    while (true) {
      if (!file->isValidOffset(cur)) {
        return false;
      }

      uint8_t current = file->readByte(cur);
      if (current == 0xFE) {
        uint8_t next = file->readByte(cur + 1);
        if (next != 0xFE) {
          uint32_t distance = (static_cast<uint32_t>(next) << 8) | file->readByte(cur + 2);
          uint32_t length = file->readByte(cur + 3);
          if (distance == 0 || distance > cur) {
            return false;
          }
          loopStack.push_back({length, cur + 4});
          cur -= distance;
          continue;
        }

        cur += 2;
        continue;
      }

      out = file->readByte(cur++);

      if (!loopStack.empty()) {
        auto &state = loopStack.back();
        if (state.bytesRemaining > 0) {
          state.bytesRemaining--;
        }
        if (state.bytesRemaining == 0) {
          cur = state.returnOffset;
          loopStack.pop_back();
        }
      }

      return true;
    }
  };

  auto copyByte = [&](uint8_t &out) -> bool {
    if (!readNextByte(out)) {
      return false;
    }
    trackData.push_back(out);
    return true;
  };

  auto copyByteDiscard = [&]() -> bool {
    uint8_t ignored = 0;
    return copyByte(ignored);
  };

  auto copyVarLen = [&]() -> bool {
    uint8_t byte = 0;
    do {
      if (!copyByte(byte)) {
        return false;
      }
    } while (byte & 0x80);
    return true;
  };

  auto copyEventDataBytes = [&](size_t count) -> bool {
    for (size_t i = 0; i < count; ++i) {
      if (!copyByteDiscard()) {
        return false;
      }
    }
    return true;
  };

  while (trackData.size() < kMaxTrackLength) {
    if (!copyVarLen()) {
      return std::nullopt;
    }

    uint8_t statusByte = 0;
    if (!copyByte(statusByte)) {
      return std::nullopt;
    }

    uint8_t status = statusByte;
    bool hasPrefetchedData = false;

    if (status < 0x80) {
      if (runningStatus == 0) {
        return std::nullopt;
      }
      hasPrefetchedData = true;
      status = runningStatus;
    } else {
      runningStatus = status;
    }

    switch (status & 0xF0) {
      case 0x80:
      case 0x90:
      case 0xA0:
      case 0xB0:
      case 0xE0: {
        if (!hasPrefetchedData) {
          if (!copyByteDiscard()) {
            return std::nullopt;
          }
        }
        if (!copyByteDiscard()) {
          return std::nullopt;
        }
        if ((status & 0xF0) == 0x90) {
          if (!copyVarLen()) {
            return std::nullopt;
          }
        }
        break;
      }

      case 0xC0:
      case 0xD0: {
        if (!hasPrefetchedData) {
          if (!copyByteDiscard()) {
            return std::nullopt;
          }
        }
        break;
      }

      case 0xF0: {
        if (status != 0xFF) {
          return std::nullopt;
        }

        uint8_t metaType = 0;
        if (hasPrefetchedData) {
          metaType = statusByte;
        } else {
          if (!copyByte(metaType)) {
            return std::nullopt;
          }
        }

        switch (metaType) {
          case 0x2D:
            if (!copyEventDataBytes(6)) {
              return std::nullopt;
            }
            break;
          case 0x2E:
            if (!copyEventDataBytes(2)) {
              return std::nullopt;
            }
            break;
          case 0x2F:
            return trackData;
          case 0x51:
            if (!copyEventDataBytes(3)) {
              return std::nullopt;
            }
            break;
          default:
            return std::nullopt;
        }
        break;
      }

      default:
        return std::nullopt;
    }
  }

  L_WARN("NinN64 track at 0x{:08X} exceeded maximum expected length", trackStart);
  return std::nullopt;
}
