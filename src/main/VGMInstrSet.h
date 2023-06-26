#pragma once
#include "common.h"
#include "VGMFile.h"
#include "DLSFile.h"
#include "SF2File.h"
#include "Menu.h"

class VGMSampColl;
class VGMInstr;
class VGMRgn;
class VGMSamp;
class VGMRgnItem;


const float defaultReverbPercent = 0.25;

// ***********
// VGMInstrSet
// ***********

class VGMInstrSet:
    public VGMFile {
 public:
  BEGIN_MENU_SUB(VGMInstrSet, VGMFile)
      MENU_ITEM(VGMInstrSet, OnSaveAsDLS, L"Convert to DLS")
      MENU_ITEM(VGMInstrSet, OnSaveAsSF2, L"Convert to SoundFont 2")
  END_MENU()

  VGMInstrSet(const std::string &format,
              RawFile *file,
              uint32_t offset,
              uint32_t length = 0,
              std::wstring name = L"VGMInstrSet",
              VGMSampColl *theSampColl = NULL);
  virtual ~VGMInstrSet(void);

  virtual bool Load();
  virtual bool GetHeaderInfo();
  virtual bool GetInstrPointers();
  virtual bool LoadInstrs();

  VGMInstr *AddInstr(uint32_t offset,
                     uint32_t length,
                     unsigned long bank,
                     unsigned long instrNum,
                     const std::wstring &instrName = L"");

  virtual FileType GetFileType() { return FILETYPE_INSTRSET; }


  bool OnSaveAsDLS(void);
  bool OnSaveAsSF2(void);
  virtual bool SaveAsDLS(const std::wstring &filepath);
  virtual bool SaveAsSF2(const std::wstring &filepath);

 public:
  std::vector<VGMInstr *> aInstrs;
  VGMSampColl *sampColl;

 protected:
  bool allowEmptyInstrs;
};





// ********
// VGMInstr
// ********

class VGMInstr:
    public VGMContainerItem {
 public:
  VGMInstr(VGMInstrSet *parInstrSet, uint32_t offset, uint32_t length, uint32_t bank,
           uint32_t instrNum, const std::wstring &name = L"Instrument", float reverb = defaultReverbPercent);
  virtual ~VGMInstr(void);

  virtual Icon GetIcon() { return ICON_INSTR; };

  inline void SetBank(uint32_t bankNum);
  inline void SetInstrNum(uint32_t theInstrNum);

  VGMRgn *AddRgn(VGMRgn *rgn);
  VGMRgn *AddRgn(uint32_t offset, uint32_t length, int sampNum, uint8_t keyLow = 0,
                 uint8_t keyHigh = 0x7F, uint8_t velLow = 0, uint8_t velHigh = 0x7F);

  virtual bool LoadInstr();

 public:
  uint32_t bank;
  uint32_t instrNum;
  float reverb;

  VGMInstrSet *parInstrSet;
  std::vector<VGMRgn *> aRgns;
};
