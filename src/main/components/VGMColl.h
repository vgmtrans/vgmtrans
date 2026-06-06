/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <span>
#include <string>
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
  void attachSeq(VGMSeq* theSeq);
  void attachInstrSet(VGMInstrSet* theInstrSet);
  void attachSampColl(VGMSampColl* theSampColl);
  void attachMiscFile(VGMMiscFile* theMiscFile);
  bool load();
  virtual bool loadMain() { return true; }

  bool containsVGMFile(const VGMFile*) const;

  std::span<VGMInstrSet* const> instrSets() const { return m_instrsets; }
  std::span<VGMSampColl* const> sampColls() const { return m_sampcolls; }
  std::span<VGMMiscFile* const> miscFiles() const { return m_miscfiles; }

 private:
  std::vector<VGMInstrSet*> m_instrsets;
  std::vector<VGMSampColl*> m_sampcolls;
  std::vector<VGMMiscFile*> m_miscfiles;

  VGMSeq* m_seq{};
  std::string m_name;
};
