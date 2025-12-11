#include "YM2151InstrSet.h"

#include "Root.h"
#include "version.h"

#include <sstream>

YM2151InstrSet::YM2151InstrSet(const std::string& format,
                               RawFile* file,
                               uint32_t offset,
                               uint32_t length,
                               std::string name)
  : VGMInstrSet(format, file, offset, length, std::move(name)) {}

std::string OPMInstrument::toOPMString(int index) const {
  std::ostringstream ss;
  ss << data.toOPMString(index);

  if (!driverName.empty()) {
    ss << "DRIVER: " << driverName;
    for (auto byte : driverData) {
      ss << " " << +byte;
    }
    ss << "\n";
  }

  return ss.str();
}

std::string YM2151InstrSet::generateOPMFile() const {
  std::ostringstream output;
  std::string header = std::string("// Converted using VGMTrans version: ") + VGMTRANS_VERSION + "\n";
  output << header;

  for (size_t i = 0; i < m_opmInstruments.size(); ++i) {
    output << m_opmInstruments[i].toOPMString(static_cast<int>(i)) << '\n';
  }
  return output.str();
}

bool YM2151InstrSet::saveAsOPMFile(const std::string &filepath) const {
  auto content = generateOPMFile();
  pRoot->UI_writeBufferToFile(filepath, reinterpret_cast<uint8_t*>(const_cast<char*>(content.data())), static_cast<uint32_t>(content.size()));
  return true;
}

void YM2151InstrSet::addOPMInstrument(OPMInstrument instrument) {
  m_opmInstruments.push_back(std::move(instrument));
}

void YM2151InstrSet::addOPMInstrument(OPMData data,
                                      std::string driverName,
                                      std::vector<uint8_t> driverData) {
  m_opmInstruments.push_back(OPMInstrument{std::move(data), std::move(driverName), std::move(driverData)});
}
