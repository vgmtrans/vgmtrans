/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "common.h"
#include <vector>

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;
class VGMFile;
class VGMSamp;
class DLSFile;
class SF2File;
class SynthFile;

class VGMColl {
 public:
  explicit VGMColl(std::string name = "Unnamed collection");
  virtual ~VGMColl() = default;

  void RemoveFileAssocs();
  [[nodiscard]] const std::string& GetName() const;
  void SetName(const std::string& newName);
  [[nodiscard]] VGMSeq *GetSeq() const;
  void UseSeq(VGMSeq *theSeq);
  void AddInstrSet(VGMInstrSet *theInstrSet);
  void AddSampColl(VGMSampColl *theSampColl);
  void AddMiscFile(VGMMiscFile *theMiscFile);
  bool Load();
  virtual bool LoadMain() { return true; }
  virtual bool CreateDLSFile(DLSFile &dls);
  virtual SF2File *CreateSF2File();
  virtual void PreSynthFileCreation() {}
  virtual SynthFile *CreateSynthFile();
  virtual bool MainDLSCreation(DLSFile &dls);
  virtual void PostSynthFileCreation() {}

  bool containsVGMFile(const VGMFile*) const;

  VGMSeq *seq{};
  std::vector<VGMInstrSet *> instrsets;
  std::vector<VGMSampColl *> sampcolls;
  std::vector<VGMMiscFile *> miscfiles;

 protected:
  static void UnpackSampColl(DLSFile &dls, const VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps);
  static void UnpackSampColl(SynthFile &synthfile, const VGMSampColl *sampColl, std::vector<VGMSamp *> &finalSamps);
  std::string name;
};
