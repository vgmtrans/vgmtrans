#pragma once
#include "common.h"
#include "VGMFile.h"
#include "helper.h"

class VGMSampColl;
class VGMInstr;
class VGMRgn;
class VGMSamp;
class VGMRgnItem;

constexpr float defaultReverbPercent = 0.25;

// ***********
// VGMInstrSet
// ***********

class VGMInstrSet : public VGMFile {
public:
  VGMInstrSet(const std::string &format, RawFile *file, uint32_t offset, uint32_t length = 0,
              std::string name = "VGMInstrSet", VGMSampColl *theSampColl = nullptr);
  ~VGMInstrSet() override;

  bool LoadVGMFile() override;
  bool Load() override;
  virtual bool GetHeaderInfo();
  virtual bool GetInstrPointers();
  virtual bool LoadInstrs();

  VGMInstr *AddInstr(uint32_t offset, uint32_t length, uint32_t bank, uint32_t instrNum,
                     const std::string &instrName = "");

  std::vector<VGMInstr *> aInstrs;
  VGMSampColl *sampColl;

protected:
   void disableAutoAddInstrumentsAsChildren() { autoAddInstrumentsAsChildren = false; }

private:
   bool autoAddInstrumentsAsChildren{true};
};

// ********
// VGMInstr
// ********

class VGMInstr : public VGMItem {
public:
  VGMInstr(VGMInstrSet *parInstrSet, uint32_t offset, uint32_t length, uint32_t bank,
           uint32_t instrNum, std::string name = "Instrument",
           float reverb = defaultReverbPercent);

  Icon GetIcon() override { return ICON_INSTR; };

  const std::vector<VGMRgn*>& regions() { return m_regions; }

  inline void SetBank(uint32_t bankNum);
  inline void SetInstrNum(uint32_t theInstrNum);

  VGMRgn *AddRgn(VGMRgn *rgn);
  VGMRgn *AddRgn(uint32_t offset, uint32_t length, int sampNum, uint8_t keyLow = 0,
                 uint8_t keyHigh = 0x7F, uint8_t velLow = 0, uint8_t velHigh = 0x7F);

  virtual bool LoadInstr();

  uint32_t bank;
  uint32_t instrNum;
  VGMInstrSet *parInstrSet;
  float reverb;


protected:
  void disableAutoAddRegionsAsChildren() { autoAddRegionsAsChildren = false; }
  void deleteRegions() { DeleteVect(m_regions); }

private:
  bool autoAddRegionsAsChildren{true};
  std::vector<VGMRgn*> m_regions;
};
