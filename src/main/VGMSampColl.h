#pragma once
#include "VGMFile.h"

class VGMInstrSet;
class VGMSamp;

// ***********
// VGMSampColl
// ***********

class VGMSampColl:
    public VGMFile {
 public:
  BEGIN_MENU_SUB(VGMSampColl, VGMFile)
      MENU_ITEM(VGMSampColl, OnSaveAllAsWav, "Save all as WAV")
  END_MENU()

  VGMSampColl(const std::string &format, RawFile *rawfile, uint32_t offset, uint32_t length = 0,
              std::string theName = "VGMSampColl");
  VGMSampColl(const std::string &format, RawFile *rawfile, VGMInstrSet *instrset,
              uint32_t offset, uint32_t length = 0, std::string theName = "VGMSampColl");
  virtual ~VGMSampColl(void);
  void UseInstrSet(VGMInstrSet *instrset) { parInstrSet = instrset; }

  virtual bool Load();
  virtual bool GetHeaderInfo();        //retrieve any header data
  virtual bool GetSampleInfo();        //retrieve sample info, including pointers to data, # channels, rate, etc.

  VGMSamp *AddSamp(uint32_t offset, uint32_t length, uint32_t dataOffset, uint32_t dataLength,
                   uint8_t nChannels = 1, uint16_t bps = 16, uint32_t theRate = 0,
                   std::string name = "Sample");
  bool OnSaveAllAsWav();

 protected:
  void LoadOnInstrMatch() { bLoadOnInstrSetMatch = true; }

 public:
  bool bLoadOnInstrSetMatch, bLoaded;

  uint32_t sampDataOffset;            //offset of the beginning of the sample data.  Used for rgn->sampOffset matching
  VGMInstrSet *parInstrSet;
  std::vector<VGMSamp *> samples;
};
