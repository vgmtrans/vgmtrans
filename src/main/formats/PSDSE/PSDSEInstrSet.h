#pragma once
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "VGMSamp.h"
#include "PSDSEHeader.h"

class PSDSESampColl : public VGMSampColl {
public:
  PSDSESampColl(const std::string& format, RawFile* rawfile, const SWDLHeader& header,
                std::string theName = "PSDSE SampColl");
  bool parseHeader() override;
  bool parseSampleInfo() override;

  SWDLHeader m_header;
};

class PSDSEInstrSet : public VGMInstrSet {
public:
  PSDSEInstrSet(RawFile* file, const SWDLHeader& header);
  ~PSDSEInstrSet() override = default;

  bool parseHeader() override;
  bool parseInstrPointers() override;
  void useColl(const VGMColl* coll) override;

  SWDLHeader m_header;
};

struct PSDSELFO {
  uint8_t mode = 0;
  uint8_t destination = 0;
  uint8_t waveform = 0;
  int32_t amplitude = 0;
  uint16_t periodMs = 0;
  uint16_t delayMs = 0;
  uint16_t fadeMs = 0;
};

class PSDSEInstr : public VGMInstr {
public:
  PSDSEInstr(VGMInstrSet* instr_set, uint32_t offset, uint32_t length, uint32_t bank, uint32_t instr_num);
  ~PSDSEInstr() override = default;

  bool loadInstr() override;

  uint8_t volume = 127;
  std::vector<PSDSELFO> lfos;
};

class PSDSESamp : public VGMSamp {
public:
  PSDSESamp(VGMSampColl* sampColl, uint32_t offset = 0, uint32_t length = 0, uint32_t dataOffset = 0,
            uint32_t dataLength = 0, uint8_t nChannels = 1, BPS bps = BPS::PCM16, uint32_t rate = 0,
            uint8_t waveType = 0, std::string name = "Sample");
  ~PSDSESamp() override = default;

  double compressionRatio() const override;
  std::vector<uint8_t> decodeImaAdpcm();
  void configureDspAdpcm(std::array<int16_t, 16> coefficients, uint32_t sampleCount, int16_t initialHistory1,
                         int16_t initialHistory2);

  static inline void clamp_step_index(int& stepIndex);
  static inline void clamp_sample(int& decompSample);
  static inline void process_nibble(unsigned char code, int& stepIndex, int& decompSample);

  enum { PCM8, PCM16, IMA_ADPCM, DSP_ADPCM };
  uint8_t waveType;

private:
  std::vector<uint8_t> decodeToNativePcm() override;
  std::vector<uint8_t> decodeDspAdpcm();

  std::array<int16_t, 16> m_dspCoefficients{};
  uint32_t m_dspSampleCount = 0;
  int16_t m_dspInitialHistory1 = 0;
  int16_t m_dspInitialHistory2 = 0;
};

class PSDSERgn : public VGMRgn {
public:
  PSDSERgn(VGMInstr* instr, uint32_t offset);
  ~PSDSERgn() override = default;

  bool loadRgn() override;
};
