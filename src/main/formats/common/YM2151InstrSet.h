#pragma once

#include "VGMInstrSet.h"
#include "YM2151.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>

class OPMInstrument {
public:
  virtual ~OPMInstrument() = default;
  virtual std::string toOPMString(int index) const = 0;
};

class BasicOPMInstrument final : public OPMInstrument {
public:
  explicit BasicOPMInstrument(OPMData data);
  std::string toOPMString(int index) const override;

private:
  OPMData m_data;
};

class CallableOPMInstrument final : public OPMInstrument {
public:
  explicit CallableOPMInstrument(std::function<std::string(int)> generator);
  std::string toOPMString(int index) const override;

private:
  std::function<std::string(int)> m_generator;
};

class YM2151InstrSet : public VGMInstrSet {
public:
  YM2151InstrSet(const std::string& format,
                 RawFile* file,
                 uint32_t offset,
                 uint32_t length,
                 std::string name);

  virtual std::string generateOPMFile() const;
  bool saveAsOPMFile(const std::string& filepath) const;

protected:
  void addOPMInstrument(OPMData data);
  void addOPMInstrument(std::function<std::string(int)> generator);
  void addOPMInstrument(std::unique_ptr<OPMInstrument> instrument);

private:
  std::vector<std::unique_ptr<OPMInstrument>> m_opmInstruments;
};
