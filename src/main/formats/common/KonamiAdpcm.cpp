/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiAdpcm.h"

#include <algorithm>
#include <span>

static const s16 K054539_DPCM_TABLE[16] = {
    0,      256,    512,    1024,
    2048,   4096,   8192,   16384,
    0,     -16384, -8192,  -4096,
   -2048,  -1024,  -512,   -256
};

// Effective K053260 deltas in 16-bit PCM units
static const s16 K053260_DPCM_TABLE_PCM[16] = {
     0,      256,    512,    1024,
   2048,    4096,   8192,   16384,
 -32768,  -16384,  -8192,  -4096,
 -2048,   -1024,   -512,    -256
};

KonamiAdpcmSamp::KonamiAdpcmSamp(
    VGMSampColl* sampColl,
    u32 offset,
    u32 length,
    KonamiAdpcmChip chip,
    u32 theRate,
    std::string name
)
    : VGMSamp(sampColl, offset, length, offset, length, 1, 16, theRate, std::move(name)),
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

std::vector<int16_t> KonamiAdpcmSamp::decodePcm16() {
  const uint32_t sampleCount = uncompressedSize() / sizeof(int16_t);
  std::vector<int16_t> samples(sampleCount);
  auto* uncompBuf = samples.data();

  size_t sampleNum = 0;          // write index in the PCM buffer
  s32 prevVal      = 0;          // “integrator” – same as the chip

  const auto emit = [&](u8 nibble) {
    // turn the 4-bit code into a delta and integrate it
    const int idx = nibble & 0x0F;
    s32 curVal = std::clamp(
        prevVal + static_cast<s32>(m_stepTable[idx]),
        -32768,
        32767
    );

    uncompBuf[sampleNum++] = static_cast<s16>(curVal);
    prevVal = curVal;
  };

     // Walk through the compressed data either forwards or backwards.
     // In reverse mode we:
     //   - start at the last byte,
     //   - step the address backwards one byte at a time
     //   - still decode low nibble first, then high nibble

  for (u32 off = dwOffset; off < dwOffset + unLength; ++off) {
    u8 b = readByte(off);
    emit(b & 0x0F);
    emit((b >> 4) & 0x0F);
  }

  return samples;
}

std::vector<uint8_t> KonamiAdpcmSamp::convertToWave(Signedness targetSignedness,
                                                    Endianness targetEndianness,
                                                    WAVE_TYPE targetWaveType) {
  std::vector<int16_t> samples = decodePcm16();
  std::span<const std::byte> srcBytes = std::as_bytes(std::span(samples));
  return convertWaveBuffer(srcBytes, targetSignedness, targetEndianness, targetWaveType);
}
