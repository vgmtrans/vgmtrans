/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "VGMInstrSet.h"
#include "YM2151.h"
#include <filesystem>

#include <cstdint>
#include <string>
#include <vector>

struct OPMInstrument {
  OPMData data;
  std::string driverName;
  std::vector<uint8_t> driverData;

  std::string toOPMString(int index) const;
};

class YM2151InstrSet : public VGMInstrSet {
public:
  YM2151InstrSet(const std::string& format,
                 RawFile* file,
                 uint32_t offset,
                 uint32_t length,
                 std::string name);

  virtual std::string generateOPMFile() const;
  bool saveAsOPMFile(const std::filesystem::path& filepath) const;

protected:
  void addOPMInstrument(OPMInstrument instrument);
  void addOPMInstrument(OPMData data,
                        std::string driverName = std::string(),
                        std::vector<uint8_t> driverData = {});

private:
  std::vector<OPMInstrument> m_opmInstruments;
};
