#pragma once
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
  PSDSESampColl(const std::string &format, RawFile *rawfile, const SWDLHeader &header,
                std::string theName = "PSDSE SampColl");
  bool parseHeader() override;
  bool parseSampleInfo() override;

  SWDLHeader m_header;
};

class PSDSEInstrSet : public VGMInstrSet {
public:
  PSDSEInstrSet(RawFile *file, const SWDLHeader &header);
  ~PSDSEInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

  SWDLHeader m_header;
};

struct PSDSELFO {
  uint8_t dest;
  uint8_t wshape;
  uint16_t rate;
  uint16_t depth;
  uint16_t delay;
  int16_t fade;
};

class PSDSEInstr : public VGMInstr {
public:
  PSDSEInstr(VGMInstrSet *instr_set, uint32_t offset, uint32_t length, uint32_t bank,
             uint32_t instr_num);
  ~PSDSEInstr() override = default;

  bool loadInstr() override;

  std::vector<PSDSELFO> lfos;
};

class PSDSESamp : public VGMSamp {
public:
  PSDSESamp(VGMSampColl *sampColl, uint32_t offset = 0, uint32_t length = 0,
            uint32_t dataOffset = 0, uint32_t dataLength = 0, uint8_t nChannels = 1,
            BPS bps = BPS::PCM16, uint32_t rate = 0, uint8_t waveType = 0,
            std::string name = "Sample");
  ~PSDSESamp() override = default;

  double compressionRatio() const override;
  std::vector<uint8_t> decodeImaAdpcm();

  static inline void clamp_step_index(int &stepIndex);
  static inline void clamp_sample(int &decompSample);
  static inline void process_nibble(unsigned char code, int &stepIndex, int &decompSample);

  enum { PCM8, PCM16, IMA_ADPCM };
  uint8_t waveType;

private:
  std::vector<uint8_t> decodeToNativePcm() override;
};

class PSDSERgn : public VGMRgn {
public:
  PSDSERgn(VGMInstr *instr, uint32_t offset);
  ~PSDSERgn() override = default;

  bool loadRgn() override;
};
