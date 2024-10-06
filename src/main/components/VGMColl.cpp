/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMColl.h"
#include "VGMSeq.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "Root.h"
#include "VGMMiscFile.h"

VGMColl::VGMColl(std::string name, std::set<SynthFileType> supportedFormats)
  : m_name(std::move(name)), m_supportedFormats(std::move(supportedFormats)) {}

void VGMColl::removeFileAssocs() {
  if (m_seq) {
    m_seq->removeCollAssoc(this);
    m_seq = nullptr;
  }

  for (auto set : m_instrsets) {
    set->removeCollAssoc(this);
  }
  for (auto samp : m_sampcolls) {
    samp->removeCollAssoc(this);
  }
  for (auto file : m_miscfiles) {
    file->removeCollAssoc(this);
  }
}

const std::string &VGMColl::name() const {
    return m_name;
}

void VGMColl::setName(const std::string& newName) {
  m_name = newName;
}

VGMSeq *VGMColl::seq() const {
  return m_seq;
}

void VGMColl::useSeq(VGMSeq *theSeq) {
  if (theSeq != nullptr)
    theSeq->addCollAssoc(this);
  if (m_seq && (theSeq != m_seq))  // if we associated with a previous sequence
    m_seq->removeCollAssoc(this);
  m_seq = theSeq;
}

void VGMColl::addInstrSet(VGMInstrSet *theInstrSet) {
  if (theInstrSet != nullptr) {
    theInstrSet->addCollAssoc(this);
    m_instrsets.push_back(theInstrSet);
  }
}

void VGMColl::addSampColl(VGMSampColl *theSampColl) {
  if (theSampColl != nullptr) {
    theSampColl->addCollAssoc(this);
    m_sampcolls.push_back(theSampColl);
  }
}

void VGMColl::addMiscFile(VGMMiscFile *theMiscFile) {
  if (theMiscFile != nullptr) {
    theMiscFile->addCollAssoc(this);
    m_miscfiles.push_back(theMiscFile);
  }
}

bool VGMColl::load() {
  if (!loadMain())
    return false;
  pRoot->addVGMColl(this);
  return true;
}

// A helper lambda function to search for a file in a vector of VGMFile*
template<typename T>
bool contains(const std::vector<T*>& vec, const VGMFile* file) {
  return std::any_of(vec.begin(), vec.end(), [file](const VGMFile* elem) {
    return elem == file;
  });
}

bool VGMColl::containsVGMFile(const VGMFile* file) const {
  // First, check if the file matches the seq property directly
  if (m_seq == file) {
    return true;
  }

  // Then, check if the file is present in any of the file vectors
  if (contains(m_instrsets, file) || contains(m_sampcolls, file) || contains(m_miscfiles, file)) {
    return true;
  }
  return false;
}

void VGMColl::addSupportedFormat(SynthFileType format) {
  m_supportedFormats.insert(format);
}

void VGMColl::removeSupportedFormat(SynthFileType format) {
  m_supportedFormats.erase(format);
}

bool VGMColl::isFormatSupported(SynthFileType format) const {
  return m_supportedFormats.find(format) != m_supportedFormats.end();
}

const std::set<SynthFileType>& VGMColl::supportedFormats() const {
  return m_supportedFormats;
}