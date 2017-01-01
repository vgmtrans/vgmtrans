#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMColl.h"
#include "VGMRgn.h"
#include "RSARFormat.h"

class RBNKInstr : public VGMInstr {
public:
  RBNKInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theInstrNum, uint32_t theBank = 0)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum) {}

private:
  enum RegionSet {
    DIRECT = 1,
    RANGE = 2,
    INDEX = 3,
    NONE = 4
  };

  struct Region {
    uint32_t high;
    uint32_t subRefOffs;
  };

  virtual bool LoadInstr() override;
  std::vector<Region> EnumerateRegionTable(uint32_t refOffset);
  void LoadRgn(VGMRgn *rgn);
};

class RSARInstrSet : public VGMInstrSet {
public:
  RSARInstrSet(RawFile *file,
    uint32_t offset,
    uint32_t length,
    std::wstring name = L"RSAR Instrument Bank",
    VGMSampColl *sampColl = NULL) : VGMInstrSet(RSARFormat::name, file, offset, length, name, sampColl) {}
private:
  virtual bool GetInstrPointers() override;
};

class RBNKSamp : public VGMSamp {
public:
  RBNKSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset, uint32_t dataLength, std::wstring name_ = L"Sample") :
    VGMSamp(sampColl, offset, length, dataOffset, dataLength) {
    name = name_;
    Load();
  }

private:
  enum Format {
    PCM8 = 0,
    PCM16 = 1,
    ADPCM = 2
  };
  Format format;

  struct AdpcmParam {
    uint16_t coef[16];
    uint16_t gain;
    uint16_t expectedScale;
    uint16_t yn1;
    uint16_t yn2;
  };

  struct ChannelParam {
    AdpcmParam adpcmParam;
    uint32_t dataOffset;
  };

  static const int MAX_CHANNELS = 2;
  ChannelParam channelParams[MAX_CHANNELS];

  uint32_t DspAddressToSamples(uint32_t dspAddr);
  void ReadAdpcmParam(uint32_t adpcmParamBase, AdpcmParam *param);
  void Load();
  void ConvertAdpcmChannel(ChannelParam *param, uint8_t *buf, int channelSpacing);
  void ConvertPCM8Channel(ChannelParam *param, uint8_t *buf, int channelSpacing);
  void ConvertPCM16Channel(ChannelParam *param, uint8_t *buf, int channelSpacing);
  void ConvertChannel(ChannelParam *param, uint8_t *buf, int channelSpacing);
  virtual void ConvertToStdWave(uint8_t *buf) override;
};

class RSARSampCollWAVE : public VGMSampColl {
public:
  RSARSampCollWAVE(RawFile *file, uint32_t offset, uint32_t length,
    uint32_t pWaveDataOffset, uint32_t pWaveDataLength,
    std::wstring name = L"RBNK WAVE Sample Collection")
    : VGMSampColl(RSARFormat::name, file, offset, length, name),
    waveDataOffset(pWaveDataOffset), waveDataLength(pWaveDataLength) {}
private:
  uint32_t waveDataOffset;
  uint32_t waveDataLength;

  virtual bool GetSampleInfo() override;
};

class RSARSampCollRWAR : public VGMSampColl {
public:
  RSARSampCollRWAR(RawFile *file, uint32_t offset, uint32_t length,
    std::wstring name = L"RWAR Sample Collection")
    : VGMSampColl(RSARFormat::name, file, offset, length, name) {}
private:
  VGMSamp * ParseRWAVFile(uint32_t offset, uint32_t index);

  virtual bool GetHeaderInfo() override;
  virtual bool GetSampleInfo() override;
};
