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

  void removeFileAssocs();
  [[nodiscard]] const std::string& name() const;
  void setName(const std::string& newName);
  [[nodiscard]] VGMSeq* seq() const;
  void useSeq(VGMSeq* theSeq);
  void addInstrSet(VGMInstrSet* theInstrSet);
  void addSampColl(VGMSampColl* theSampColl);
  void addMiscFile(VGMMiscFile* theMiscFile);
  bool load();
  virtual bool loadMain() { return true; }
  virtual bool createDLSFile(DLSFile& dls);
  virtual SF2File* createSF2File();
  virtual void preSynthFileCreation() {}
  virtual SynthFile* createSynthFile();
  virtual bool mainDLSCreation(DLSFile& dls);
  virtual void postSynthFileCreation() {}

  bool containsVGMFile(const VGMFile*) const;

  const std::vector<VGMInstrSet*>& instrSets() { return m_instrsets; }
  const std::vector<VGMSampColl*>& sampColls() { return m_sampcolls; }
  const std::vector<VGMMiscFile*>& miscFiles() { return m_miscfiles; }

 private:
  static void unpackSampColl(DLSFile& dls, const VGMSampColl* sampColl, std::vector<VGMSamp*>& finalSamps);
  static void unpackSampColl(SynthFile& synthfile, const VGMSampColl* sampColl, std::vector<VGMSamp*>& finalSamps);

  std::vector<VGMInstrSet*> m_instrsets;
  std::vector<VGMSampColl*> m_sampcolls;
  std::vector<VGMMiscFile*> m_miscfiles;

  VGMSeq* m_seq{};
  std::string m_name;
};
