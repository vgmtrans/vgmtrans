#pragma once

#include "VGMInstrSet.h"
#include "YM2151.h"

#include <string>
#include <vector>
#include <utility>

class YM2151InstrSet : public VGMInstrSet {
public:
  YM2151InstrSet(const std::string& format,
                 RawFile* file,
                 uint32_t offset,
                 uint32_t length,
                 std::string name);

  std::string generateOPMFile() const;
  bool saveAsOPMFile(const std::string& filepath) const;

protected:
  void addOPMInstrument(OPMData data);

private:
  std::vector<OPMData> m_opmInstruments;
};
