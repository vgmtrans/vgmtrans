#pragma once

#include "base/Types.h"

#include "VGMFile.h"
#include "VGMSamp.h"

class VGMInstrSet;
class VGMSamp;

// ***********
// VGMSampColl
// ***********

class VGMSampColl : public VGMFile {
 public:
  VGMSampColl(const std::string &format, RawFile *rawfile, u32 offset, u32 length = 0,
              std::string theName = "VGMSampColl");
  VGMSampColl(const std::string &format, RawFile *rawfile, VGMInstrSet *instrset, u32 offset,
                u32 length = 0, std::string theName = "VGMSampColl");
  void useInstrSet(VGMInstrSet *instrset) { parInstrSet = instrset; }

  bool loadVGMFile(bool useMatcher = true) override;
  bool load() override;
  virtual bool parseHeader();        // retrieve any header data
  virtual bool parseSampleInfo();        // retrieve sample info, including pointers to data, # channels, rate, etc.

  VGMSamp *addSamp(u32 offset, u32 length, u32 dataOffset, u32 dataLength,
                   u8 nChannels = 1, BPS bps = BPS::PCM16, u32 theRate = 0,
                   std::string name = "Sample");

  bool shouldLoadOnInstrSetMatch() const { return m_should_load_on_instr_set_match; }

public:
  u32 sampDataOffset;        // offset of the beginning of the sample data.  Used for rgn->sampOffset matching
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
