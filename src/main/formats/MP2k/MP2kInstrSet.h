/*
 * VGMTrans (c) 2002-2019
 * VGMTransQt (c) 2020
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <map>
#include "VGMInstrSet.h"
#include "VGMSamp.h"

class MP2kInstrSet final : public VGMInstrSet {
public:
  MP2kInstrSet(RawFile *file, int rate, size_t offset, int count,
               const std::string &name = "MP2K Instrument bank");
  ~MP2kInstrSet() = default;

  bool loadInstrs() override;
  bool parseInstrPointers() override;
  int makeOrGetSample(size_t sample_pointer);
  int sampleRate() const noexcept { return m_operating_rate; };

private:
  int m_count = 0;
  int m_operating_rate = 22050;
  std::map<size_t, int> m_samples;
};

struct MP2kInstrData {
  u32 w0;
  u32 w1;
  u32 w2;
};

class MP2kInstr final : public VGMInstr {
public:
  MP2kInstr(MP2kInstrSet *set, size_t offset, size_t length, u32 bank, u32 number,
            MP2kInstrData data);
  ~MP2kInstr() = default;

  bool loadInstr() override;

private:
  void setADSR(VGMRgn *dest, u32 data);

  u8 m_type = 0;
  MP2kInstrData m_data;
};

enum class MP2kWaveType { PCM8, BDPCM };

class MP2kSamp final : public VGMSamp {
public:
  MP2kSamp(VGMSampColl *sampColl, MP2kWaveType type, uint32_t offset = 0, uint32_t length = 0,
           uint32_t dataOffset = 0, uint32_t dataLength = 0, uint8_t channels = 1,
           uint16_t bps = 16, uint32_t rate = 0, uint8_t waveType = 0, std::string name = "Sample");
  ~MP2kSamp() = default;

private:
  std::vector<uint8_t> decode() override;
  MP2kWaveType m_type;
};
