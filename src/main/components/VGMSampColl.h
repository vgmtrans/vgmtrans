#pragma once

#include "VGMFile.h"

class VGMInstrSet;
class VGMSamp;

// ***********
// VGMSampColl
// ***********

class VGMSampColl : public VGMFile {
 public:
  VGMSampColl(const std::string &format, RawFile *rawfile, uint32_t offset, uint32_t length = 0,
              std::string theName = "VGMSampColl");
  VGMSampColl(const std::string &format, RawFile *rawfile, VGMInstrSet *instrset, uint32_t offset,
                uint32_t length = 0, std::string theName = "VGMSampColl");
  void useInstrSet(VGMInstrSet *instrset) { parInstrSet = instrset; }

  bool loadVGMFile(bool useMatcher = true) override;
  bool load() override;
  virtual bool parseHeader();        // retrieve any header data
  virtual bool parseSampleInfo();        // retrieve sample info, including pointers to data, # channels, rate, etc.

  VGMSamp *addSamp(uint32_t offset, uint32_t length, uint32_t dataOffset, uint32_t dataLength,
                   uint8_t nChannels = 1, uint16_t bps = 16, uint32_t theRate = 0,
                   std::string name = "Sample");

  bool shouldLoadOnInstrSetMatch() const { return m_should_load_on_instr_set_match; }

public:
  uint32_t sampDataOffset;        // offset of the beginning of the sample data.  Used for rgn->sampOffset matching
  VGMInstrSet *parInstrSet;
  std::vector<VGMSamp *> samples;

protected:
  void setShouldLoadOnInstrSetMatch(bool should_load) { m_should_load_on_instr_set_match = should_load; }

private:
  bool m_should_load_on_instr_set_match, bLoaded;
};

namespace conversion {
void saveAsWAV(const VGMSampColl &coll, const std::string &save_dir);
}
