/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiAdpcm.h"

#include <algorithm>

static const int16_t K054539_DPCM_TABLE[16] = {
    0,      256,    512,    1024,
    2048,   4096,   8192,   16384,
    0,     -16384, -8192,  -4096,
   -2048,  -1024,  -512,   -256
};

// Effective K053260 deltas in 16-bit PCM units
static const int16_t K053260_DPCM_TABLE_PCM[16] = {
     0,      256,    512,    1024,
   2048,    4096,   8192,   16384,
 -32768,  -16384,  -8192,  -4096,
 -2048,   -1024,   -512,    -256
};

KonamiAdpcmSamp::KonamiAdpcmSamp(
    VGMSampColl* sampColl,
    uint32_t offset,
    uint32_t length,
    KonamiAdpcmChip chip,
    uint32_t theRate,
    std::string name
)
    : VGMSamp(sampColl, offset, length, offset, length, 1, BPS::PCM16, theRate, std::move(name)),
      m_chip(chip),
      m_stepTable(nullptr) {
  switch (m_chip) {
  case KonamiAdpcmChip::K054539:
    m_stepTable = K054539_DPCM_TABLE;
    break;
  case KonamiAdpcmChip::K053260:
    m_stepTable = K053260_DPCM_TABLE_PCM;
    break;
  }
}

double KonamiAdpcmSamp::compressionRatio() const {
  return (16.0 / 4); // 4 bit samples converted up to 16 bit samples
}

std::vector<uint8_t> KonamiAdpcmSamp::decodeToNativePcm() {
  const uint32_t sampleCount = uncompressedSize() / sizeof(int16_t);
  std::vector<uint8_t> samples(sampleCount * sizeof(int16_t));
  auto* uncompBuf = reinterpret_cast<int16_t*>(samples.data());

  size_t sampleNum = 0;          // write index in the PCM buffer
  int32_t prevVal      = 0;          // “integrator” – same as the chip

  const auto emit = [&](uint8_t nibble) {
    // turn the 4-bit code into a delta and integrate it
    const int idx = nibble & 0x0F;
    int32_t curVal = std::clamp(
        prevVal + static_cast<int32_t>(m_stepTable[idx]),
        -32768,
        32767
    );

    uncompBuf[sampleNum++] = static_cast<int16_t>(curVal);
    prevVal = curVal;
  };

     // Walk through the compressed data either forwards or backwards.
     // In reverse mode we:
     //   - start at the last byte,
     //   - step the address backwards one byte at a time
     //   - still decode low nibble first, then high nibble
  if (reverse()) {
    for (uint32_t off = offset() + length();  off-- > offset(); ) {
      uint8_t b = readByte(off);
      emit( b & 0x0F );
      emit( (b >> 4) & 0x0F );
    }
  }
  else {
    for (uint32_t off = offset(); off < offset() + length(); ++off) {
      uint8_t b = readByte(off);
      emit( b & 0x0F );
      emit( (b >> 4) & 0x0F );
    }
  }
  return samples;
}
