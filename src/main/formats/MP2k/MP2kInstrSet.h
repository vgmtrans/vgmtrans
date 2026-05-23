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
#include "VGMSampColl.h"

class MP2kPSGColl;

class MP2kInstrSet final : public VGMInstrSet {
public:
  MP2kInstrSet(RawFile *file, int rate, size_t offset, int count, MP2kPSGColl *psg_samples,
               const std::string &name = "MP2K Instrument bank");
  ~MP2kInstrSet() = default;

  bool loadInstrs() override;
  bool parseInstrPointers() override;
  int makeOrGetSample(size_t sample_pointer);
  int sampleRate() const noexcept { return m_operating_rate; };
  MP2kPSGColl& psgSampColl() const noexcept;

private:
  int m_count = 0;
  int m_operating_rate = 22050;
  std::map<size_t, int> m_samples;
  MP2kPSGColl* m_psg_samples{};
};

struct MP2kInstrData {
  uint32_t w0;
  uint32_t w1;
  uint32_t w2;
};

class MP2kInstr final : public VGMInstr {
public:
  MP2kInstr(MP2kInstrSet *set, size_t offset, size_t length, uint32_t bank, uint32_t number,
            MP2kInstrData data);
  ~MP2kInstr() = default;

  bool loadInstr() override;

private:
  void setADSR(VGMRgn *dest, uint32_t data);
  void setCgbADSR(VGMRgn *dest, uint32_t data);

  uint8_t m_type = 0;
  MP2kInstrData m_data;
};

enum class MP2kWaveType { PCM8, BDPCM };

struct MP2kSampParams {
  MP2kWaveType type;
  uint32_t offset;
  uint32_t length;
  uint32_t dataOffset;
  uint32_t dataLength;
};

class MP2kSamp final : public VGMSamp {
public:
  MP2kSamp(VGMSampColl *sampColl, MP2kSampParams params);
  ~MP2kSamp() = default;

private:
  std::vector<uint8_t> decodeToNativePcm() override;
  MP2kWaveType m_type;
};

class MP2kPSGColl final : public VGMSampColl {
public:
  MP2kPSGColl(RawFile *file, uint32_t sampleRate, uint32_t loopSamples);
  ~MP2kPSGColl() override = default;

  int makeOrGetProgrammableWave(size_t wavePointer);

private:
  bool parseSampleInfo() override;

  uint32_t m_sample_rate;
  uint32_t m_loop_samples;
  std::map<size_t, int> m_programmable_waves;
};

class MP2kPSGSamp final : public VGMSamp {
public:
  MP2kPSGSamp(VGMSampColl *sampColl, uint8_t dutyIndex, bool noise, uint32_t sampleRate,
              uint32_t loopSamples, std::string name);
  ~MP2kPSGSamp() override = default;

private:
  std::vector<uint8_t> decodeToNativePcm() override;

  double m_duty_ratio;
  bool m_noise;
};

class MP2kPSGWaveSamp final : public VGMSamp {
public:
  MP2kPSGWaveSamp(VGMSampColl *sampColl, size_t wavePointer, uint32_t sampleRate,
                  std::string name);
  ~MP2kPSGWaveSamp() override = default;

private:
  std::vector<uint8_t> decodeToNativePcm() override;

  size_t m_wave_pointer;
};
