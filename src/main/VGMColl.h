#pragma once

#include "common.h"
#include "Menu.h"

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMSamp;
class DLSFile;
class SF2File;
class SynthFile;

class VGMColl
    : public VGMItem {
 public:
  BEGIN_MENU(VGMColl)
      MENU_ITEM(VGMColl, OnSaveAllDLS, L"Save as MIDI and DLS.")
      MENU_ITEM(VGMColl, OnSaveAllSF2, L"Save as MIDI and SoundFont 2.")
      //MENU_ITEM(VGMFile, OnSaveAllAsRaw, L"Save all as original format")
  END_MENU()

  VGMColl(std::wstring name = L"Unnamed Collection");
  virtual ~VGMColl(void);

  void RemoveFileAssocs();
  const std::wstring *GetName(void) const;
  void SetName(const std::wstring *newName);
  VGMSeq *GetSeq();
  void UseSeq(VGMSeq *theSeq);
  void AddInstrSet(VGMInstrSet *theInstrSet);
  void AddSampColl(VGMSampColl *theSampColl);
  void AddMiscFile(VGMFile *theMiscFile);
  bool Load();
  virtual bool LoadMain() { return true; }
  virtual bool CreateDLSFile(DLSFile &dls);
  virtual SF2File *CreateSF2File();
  virtual void PreSynthFileCreation() {}
  virtual SynthFile *CreateSynthFile();
  virtual bool MainDLSCreation(DLSFile &dls);
  virtual void PostSynthFileCreation() {}

  bool OnSaveAllDLS();
  bool OnSaveAllSF2();

  VGMSeq *seq;
  std::vector<VGMInstrSet *> instrsets;
  std::vector<VGMSampColl *> sampcolls;
  std::vector<VGMFile *> miscfiles;

 protected:
  void UnpackSampColl(DLSFile &dls, VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps);
  void UnpackSampColl(SynthFile &synthfile, VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps);

 protected:
  std::wstring name;
};
