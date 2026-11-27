#pragma once

#include "VGMSamp.h"
#include "VGMSampColl.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class RawFile;

struct PSDSEDspStreamChannel {
  uint32_t sampleCount = 0;
  uint32_t nibbleCount = 0;
  uint32_t sampleRate = 0;
  uint16_t loopFlag = 0;
  uint32_t loopStart = 0;
  uint32_t loopEnd = 0;
  uint16_t initialPredictorScale = 0;
  int16_t initialHistory1 = 0;
  int16_t initialHistory2 = 0;
  std::array<int16_t, 16> coefficients{};
};

struct PSDSESADBHeader {
  uint32_t offset = 0;
  uint32_t fileLength = 0;
  uint16_t version = 0;
  uint16_t fileId = 0;
  std::string internalName;
  uint8_t channels = 0;
  uint16_t interleave = 0;
  uint32_t encodedDataEnd = 0;
  uint32_t encodedBytesPerChannel = 0;
  uint32_t dataOffset = 0;
  uint32_t sampleCount = 0;
  uint32_t sampleRate = 0;
  uint8_t volume = 127;
  uint8_t pan = 64;
  std::array<PSDSEDspStreamChannel, 2> channel{};

  bool read(const RawFile* file, uint32_t readOffset);
};

class PSDSESADBSampColl : public VGMSampColl {
public:
  PSDSESADBSampColl(RawFile* file, const PSDSESADBHeader& header);

  bool parseHeader() override;
  bool parseSampleInfo() override;

private:
  PSDSESADBHeader m_header;
};

class PSDSESADBSamp : public VGMSamp {
public:
  PSDSESADBSamp(VGMSampColl* sampColl, const PSDSESADBHeader& header);

  double compressionRatio() const override;

private:
  std::vector<uint8_t> decodeToNativePcm() override;
  uint8_t encodedByte(uint8_t channel, uint32_t channelOffset) const;

  PSDSESADBHeader m_header;
};
