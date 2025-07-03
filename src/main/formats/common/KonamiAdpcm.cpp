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

void K054539AdpcmSamp::convertToStdWave(u8* buf)
{
  auto* uncompBuf = reinterpret_cast<s16*>(buf);

  std::size_t sampleNum = 0;          // write index in the PCM buffer
  std::int32_t prevVal  = 0;          // “integrator” – same as the chip

  const auto emit = [&](u8 nibble) {
    // turn the 4-bit code into a delta and integrate it
    std::int32_t curVal = std::clamp(prevVal + DPCM_TABLE[nibble], -32768, 32767);

    uncompBuf[sampleNum++] = static_cast<s16>(curVal);
    prevVal = curVal;
  };

     // Walk through the compressed data either forwards or backwards.
     // In *reverse* mode we:
     //   • start at the last byte,
     //   • step the address backwards one byte at a time
     //   • still decode *low nibble first,* then high nibble.

  if (!reverse()) {
    for (std::uint32_t off = dwOffset; off < dwOffset + unLength; ++off) {
      std::uint8_t b = readByte(off);
      emit( b        & 0x0F );
      emit( b >> 4   & 0x0F );
    }
  }
  else {
    for (std::uint32_t off = dwOffset + unLength;  off-- > dwOffset; ) {
      std::uint8_t b = readByte(off);
      emit( b        & 0x0F );
      emit( b >> 4   & 0x0F );
    }
  }
}