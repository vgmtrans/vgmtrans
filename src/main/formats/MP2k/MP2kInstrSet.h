/*
 * VGMTrans (c) 2002-2019
 * VGMTransQt (c) 2020
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "MP2kDriver.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class MP2kPSGColl;

class MP2kInstrSet final : public VGMInstrSet {
public:
  MP2kInstrSet(RawFile *file, int rate, size_t offset, int count, MP2kPSGColl *psg_samples,
               MP2kDriverVariant variant = MP2kDriverVariant::Standard,
               const std::string &name = "MP2K Instrument bank");
  ~MP2kInstrSet() = default;

  bool loadInstrs() override;
  bool parseInstrPointers() override;
  int makeOrGetSample(size_t sample_pointer);
  bool isBonSample(size_t pointer) const;
  int makeOrGetBonSample(size_t pointer, u8 key);
  int sampleRate() const noexcept { return m_operating_rate; };
  double pcmGain() const noexcept { return (m_master_volume + 1) / 16.0; }
  // Mix gains are calibrated against the standard Sappy mixer only; Nintendo R&D1 drivers
  // route PSG/FIFO output differently and keep their previous levels.
  bool usesCalibratedMixGain() const noexcept { return !isNintendoRd1Driver(m_variant); }
  void setPcmMasterVolume(u8 volume);
  MP2kPSGColl& psgSampColl() const noexcept;

private:
  int m_count = 0;
  int m_operating_rate = 22050;
  u8 m_master_volume = 15;
  MP2kDriverVariant m_variant;
  std::map<size_t, int> m_samples;
  std::map<std::pair<size_t, u8>, int> m_bon_samples;
  MP2kPSGColl* m_psg_samples{};
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
  void setCgbADSR(VGMRgn *dest, u32 data);

  MP2kInstrData m_data;
};

enum class MP2kWaveType { PCM8, BDPCM };

struct MP2kSampParams {
  MP2kWaveType type;
  u32 offset;
  u32 length;
  u32 dataOffset;
  u32 dataLength;
};

class MP2kSamp final : public VGMSamp {
public:
  MP2kSamp(VGMSampColl *sampColl, MP2kSampParams params);
  ~MP2kSamp() = default;

private:
  std::vector<u8> decodeToNativePcm() override;
  MP2kWaveType m_type;
};

class MP2kPSGColl final : public VGMSampColl {
public:
  MP2kPSGColl(RawFile *file, u32 sampleRate, u32 loopSamples, u32 noiseSampleRate = 65536);
  ~MP2kPSGColl() override = default;

  int makeOrGetProgrammableWave(size_t wavePointer);
  int makeOrGetNoise(u8 key, bool shortMode);

private:
  bool parseSampleInfo() override;

  u32 m_sample_rate;
  u32 m_loop_samples;
  u32 m_noise_sample_rate;
  std::map<size_t, int> m_programmable_waves;
  std::map<u8, int> m_noise_samples;
};

class MP2kPSGSamp final : public VGMSamp {
public:
  MP2kPSGSamp(VGMSampColl *sampColl, u8 dutyIndex, bool noise, u32 sampleRate,
              u32 loopSamples, std::string name);
  ~MP2kPSGSamp() override = default;

private:
  std::vector<u8> decodeToNativePcm() override;

  double m_duty_ratio;
  bool m_noise;
};

class MP2kPSGWaveSamp final : public VGMSamp {
public:
  MP2kPSGWaveSamp(VGMSampColl *sampColl, size_t wavePointer, u32 sampleRate,
                  std::string name);
  ~MP2kPSGWaveSamp() override = default;

private:
  std::vector<u8> decodeToNativePcm() override;

  size_t m_wave_pointer;
};

class MP2kPSGNoiseSamp final : public VGMSamp {
public:
  MP2kPSGNoiseSamp(VGMSampColl* sampColl, u8 control, u32 sampleRate, u32 loopSamples,
                   std::string name);

private:
  std::vector<u8> decodeToNativePcm() override;
  u8 m_control;
};

class MP2kBonSamp final : public VGMSamp {
public:
  enum class Shape : u8 { Pulse, Saw, Triangle };

  struct PwmSettings {
    u8 base;
    u8 step;
    u8 depth;
    u8 initialPhase;
  };

  struct RenderSettings {
    Shape shape;
    PwmSettings pwm;
    u8 key;
    u32 sampleRate;
    u32 loopSamples;
    u32 carrierCycles;
    s16 fineTune;
  };

  MP2kBonSamp(VGMSampColl* collection, size_t pointer, const RenderSettings& settings);

private:
  std::vector<u8> decodeToNativePcm() override;
  RenderSettings m_settings;
};
