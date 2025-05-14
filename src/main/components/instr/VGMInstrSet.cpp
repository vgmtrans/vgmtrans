/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMInstrSet.h"
#include <spdlog/fmt/fmt.h>
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMRgn.h"
#include "VGMColl.h"
#include "Root.h"
#include "Format.h"
#include "LogManager.h"
#include "helper.h"

// ***********
// VGMInstrSet
// ***********

VGMInstrSet::VGMInstrSet(const std::string &format, RawFile *file, uint32_t offset, uint32_t length,
                         std::string name, VGMSampColl *theSampColl)
    : VGMFile(format, file, offset, length, std::move(name)),
      sampColl(theSampColl) {
}

VGMInstrSet::~VGMInstrSet() {
  delete sampColl;
}

VGMInstr *VGMInstrSet::addInstr(uint32_t offset, uint32_t length, uint32_t bank,
                                uint32_t instrNum, const std::string &instrName) {
  VGMInstr *instr =
      new VGMInstr(this, offset, length, bank, instrNum,
                   instrName.empty() ? fmt::format("Instrument {}", aInstrs.size()) : instrName);
  aInstrs.push_back(instr);
  return instr;
}

bool VGMInstrSet::loadVGMFile() {
  bool val = load();
  if (!val) {
    return false;
  }

  if (auto fmt = format(); fmt) {
    fmt->onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>(this));
  }

  return val;
}

bool VGMInstrSet::load() {
  if (!parseHeader())
    return false;
  if (!parseInstrPointers())
    return false;
  if (!loadInstrs())
    return false;

  if (m_auto_add_instruments_as_children)
    addChildren(aInstrs);

  if (unLength == 0) {
    setGuessedLength();
  }

  if (sampColl != nullptr) {
    if (!sampColl->load()) {
      L_WARN("Failed to load VGMSampColl");
    } else {
      sampColl->transferChildren(this);
    }
  }

  rawFile()->addContainedVGMFile(
      std::make_shared<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>>(this));
  pRoot->addVGMFile(this);
  return true;
}

bool VGMInstrSet::parseHeader() {
  return true;
}

bool VGMInstrSet::parseInstrPointers() {
  return true;
}

bool VGMInstrSet::loadInstrs() {
  size_t nInstrs = aInstrs.size();
  for (size_t i = 0; i < nInstrs; i++) {
    if (!aInstrs[i]->loadInstr())
      return false;
  }
  return true;
}

// ********
// VGMInstr
// ********

VGMInstr::VGMInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                   uint32_t theInstrNum, std::string name, float reverb)
    : VGMItem(instrSet, offset, length, std::move(name), Type::Instrument),
      bank(theBank), instrNum(theInstrNum), parInstrSet(instrSet), reverb(reverb) {
}

void VGMInstr::setBank(uint32_t bankNum) {
  bank = bankNum;
}

void VGMInstr::setInstrNum(uint32_t theInstrNum) {
  instrNum = theInstrNum;
}

VGMRgn *VGMInstr::addRgn(VGMRgn *rgn) {
  m_regions.emplace_back(rgn);
  if (m_auto_add_regions_as_children)
    addChild(rgn);
  return rgn;
}

VGMRgn *VGMInstr::addRgn(uint32_t offset, uint32_t length, int sampNum, uint8_t keyLow,
                         uint8_t keyHigh, uint8_t velLow, uint8_t velHigh) {
  VGMRgn *newRgn = new VGMRgn(this, offset, length, keyLow, keyHigh, velLow, velHigh, sampNum);
  m_regions.emplace_back(newRgn);
  if (m_auto_add_regions_as_children)
    addChild(newRgn);
  return newRgn;
}

void VGMInstr::deleteRegions() {
  deleteVect(m_regions);
}
