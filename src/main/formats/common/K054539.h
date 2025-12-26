#pragma once

#include "ScaleConversion.h"
#include "constevalHelpers.h"
#include "common.h"
#include "VGMFile.h"
#include <array>

enum class k054539_sample_type: u8 {
  PCM_8 = 0,
  PCM_16 = 4,
  ADPCM = 8,
};

template <std::size_t N = 256>
consteval std::array<double, N> make_volume_table() {
  std::array<double, N> table{};
  for (std::size_t i = 0; i < N; ++i) {
    // The MAME implementation divides these values by 4, this causes them to be too quiet
    table[i] = ct::pow(10.0, (-36.0 * static_cast<double>(i) / 64.0) / 20.0);
  }
  return table;
}

template <std::size_t N = 0xF>
consteval std::array<double, N> make_pan_table() {
  std::array<double, N> table{};
  for (std::size_t i = 0; i < N; ++i) {
    table[i] = ct::sqrt(static_cast<double>(i)) / ct::sqrt(static_cast<double>(N - 1));
  }
  return table;
}

constexpr auto volTable = make_volume_table();
constexpr auto panTable = make_pan_table();

inline u8 calculateMidiPanForK054539(u8 pan) {
  if (pan >= 0x81 && pan <= 0x8f)
    pan -= 0x81;
  else if (pan >= 0x11 && pan <= 0x1f)
    pan -= 0x11;
  else
    pan = 0x18 - 0x11;
  double rightPan = panTable[pan];
  double leftPan = panTable[0xE - pan];
  double volumeScale;
  u8 newPan = convertVolumeBalanceToStdMidiPan(leftPan, rightPan, &volumeScale);
  return newPan;
}

inline u32 determineK054539SampleSize(VGMFile* file, u32 startOffset, k054539_sample_type sampleType, bool reverse) {
  // Each sample type uses a slightly different sentinel value.
  // In practice, we find that each sample ends with multiple sample values. The smallest pattern
  // being ADPCM with 4 0x88 bytes in a row.
  u16 endMarker;
  int inc = 1;
  switch (sampleType) {
    case k054539_sample_type::PCM_8:
      endMarker = 0x8080;
      break;
    case k054539_sample_type::PCM_16:
      endMarker = 0x8000;
      inc = 2;
      break;
    case k054539_sample_type::ADPCM:
      endMarker = 0x8888;
      break;
    default:
      endMarker = 0x8080;
      break;
  }

  if (reverse) {
    for (u32 off = startOffset-2; off >= file->dwOffset + 2; off -= inc) {
      if (file->readShort(off) == endMarker) {
        return startOffset - off;
      }
    }
    return startOffset;
  }
  for (u32 off = startOffset; off < file->unLength + 2; off += inc) {
    if (file->readShort(off) == endMarker) {
      return off - startOffset;
    }
  }
  return file->unLength - startOffset;
}