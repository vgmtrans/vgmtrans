#pragma once

#include "base/Types.h"
#include "VGMFile.h"
#include "VGMSamp.h"

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

class VGMInstrSet;
class VGMSamp;

// ***********
// VGMSampColl
// ***********

class VGMSampColl : public VGMFile {
 public:
  VGMSampColl(const std::string &format, RawFile *rawfile, u32 offset, u32 length = 0,
              std::string name = "VGMSampColl");
  VGMSampColl(const std::string &format, RawFile *rawfile, VGMInstrSet *instrset, u32 offset,
                u32 length = 0, std::string name = "VGMSampColl");
  ~VGMSampColl() override;
  void attachInstrSet(VGMInstrSet *instrset) { parInstrSet = instrset; }

  bool load() override;
  virtual bool parseHeader();        // retrieve any header data
  virtual bool parseSampleInfo();        // retrieve sample info, including pointers to data, # channels, rate, etc.

  VGMSamp *addSamp(u32 offset, u32 length, u32 dataOffset, u32 dataLength,
                   u8 nChannels = 1, BPS bps = BPS::PCM16, u32 rate = 0,
                   std::string name = "Sample");
  VGMSamp *sinkSamp(std::unique_ptr<VGMSamp>&& samp);
  template <class SampType, class... Args>
  SampType* addSamp(Args&&... args) {
    auto samp = std::make_unique<SampType>(std::forward<Args>(args)...);
    auto* rawSamp = samp.get();
    sinkSamp(std::move(samp));
    return rawSamp;
  }

  [[nodiscard]] std::span<VGMSamp* const> samples() const { return m_samples; }
  [[nodiscard]] bool hasSamples() const { return !m_samples.empty(); }
  [[nodiscard]] size_t sampleCount() const { return m_samples.size(); }
  [[nodiscard]] VGMSamp* sample(size_t index) const { return m_samples.at(index); }

  bool shouldLoadOnInstrSetMatch() const { return m_should_load_on_instr_set_match; }

public:
  u32 sampDataOffset;        // offset of the beginning of the sample data.  Used for rgn->sampOffset matching
  VGMInstrSet *parInstrSet;

protected:
  void setShouldLoadOnInstrSetMatch(bool should_load) { m_should_load_on_instr_set_match = should_load; }
  void reserveSamples(size_t count) { m_ownedSamples.reserve(count); m_samples.reserve(count); }
  void clearSamples();

private:
  bool m_should_load_on_instr_set_match, bLoaded;
  std::vector<std::unique_ptr<VGMSamp>> m_ownedSamples;
  std::vector<VGMSamp*> m_samples;
};

namespace conversion {
void saveAsWAV(const VGMSampColl &coll, const std::string &save_dir);
}
