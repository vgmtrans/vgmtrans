#include "KonamiAdpcm.h"

static const int16_t DPCM_TABLE[16] = {
    0, 256, 512, 1024, 2048, 4096, 8192, 16384, 0, -16384, -8192, -4096, -2048, -1024, -512, -256
};

double K054539AdpcmSamp::compressionRatio() {
  return (16.0 / 4); // 4 bit samples converted up to 16 bit samples
}

K054539AdpcmSamp::K054539AdpcmSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length,
                                     uint32_t theRate, std::string name)
    : VGMSamp(sampColl, offset, length, offset, length, 1, 16, theRate, std::move(name)) {}

void K054539AdpcmSamp::convertToStdWave(uint8_t *buf) {
  auto* uncompBuf = reinterpret_cast<int16_t*>(buf);

  int32_t curVal = 0; // Current PCM sample value
  int32_t prevVal = 0; // Previous PCM sample value
  int sampleNum = 0;

  for (uint32_t off = dwOffset; off < (dwOffset + unLength); ++off) {
    uint8_t sampleByte = readByte(off);

    // Process both nibbles (4-bit samples) in the byte
    for (int nibble_shift = 0; nibble_shift <= 4; nibble_shift += 4) {
      uint8_t nibble = (sampleByte >> nibble_shift) & 0x0F;  // Extract 4-bit value
      int16_t delta = DPCM_TABLE[nibble];  // Get corresponding delta from DPCM table

      // Apply the delta to the previous value
      curVal = prevVal + delta;

      // Saturate to the range of a 16-bit PCM sample
      curVal = std::clamp(curVal, -32768, 32767);

      // Append the computed PCM value to the output
      uncompBuf[sampleNum++] = static_cast<int16_t>(curVal);

      // Update previous value for the next sample
      prevVal = curVal;
    }
  }
}
