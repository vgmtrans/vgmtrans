/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMInstrSet.h"
#include <spdlog/fmt/fmt.h>
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "VGMColl.h"
#include "Root.h"
#include "Format.h"
#include "LogManager.h"

// ***********
// VGMInstrSet
// ***********

VGMInstrSet::VGMInstrSet(const std::string &format, RawFile *file, uint32_t offset, uint32_t length,
                         std::string name, VGMSampColl *theSampColl)
    : VGMFile(format, file, offset, length, std::move(name)), sampColl(theSampColl) {
}

VGMInstrSet::~VGMInstrSet() {
  delete sampColl;
}

VGMInstr *VGMInstrSet::AddInstr(uint32_t offset, uint32_t length, uint32_t bank,
                                uint32_t instrNum, const std::string &instrName) {
  VGMInstr *instr =
      new VGMInstr(this, offset, length, bank, instrNum,
                   instrName.empty() ? fmt::format("Instrument {}", aInstrs.size()) : instrName);
  aInstrs.push_back(instr);
  return instr;
}

void VGMInstrSet::setInstrsParentItem(VGMItem* item) {
  m_instrs_parent = item;
}

bool VGMInstrSet::LoadVGMFile() {
  bool val = Load();
  if (!val) {
    return false;
  }

  if (auto fmt = format(); fmt) {
    fmt->OnNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>(this));
  }

  return val;
}

bool VGMInstrSet::Load() {
  if (!GetHeaderInfo())
    return false;
  if (!GetInstrPointers())
    return false;
  if (!LoadInstrs())
    return false;

  if (aInstrs.empty())
    return false;

  m_instrs_parent->addChildren(aInstrs);

  if (unLength == 0) {
    SetGuessedLength();
  }

  if (sampColl != nullptr) {
    if (!sampColl->Load()) {
      L_WARN("Failed to load VGMSampColl");
    }
  }

  rawFile()->AddContainedVGMFile(
      std::make_shared<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>>(this));
  pRoot->AddVGMFile(this);
  return true;
}

bool VGMInstrSet::GetHeaderInfo() {
  return true;
}

bool VGMInstrSet::GetInstrPointers() {
  return true;
}

bool VGMInstrSet::LoadInstrs() {
  size_t nInstrs = aInstrs.size();
  for (size_t i = 0; i < nInstrs; i++) {
    if (!aInstrs[i]->LoadInstr())
      return false;
  }
  return true;
}

// ********
// VGMInstr
// ********

VGMInstr::VGMInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                   uint32_t theInstrNum, std::string name, float reverb)
    : VGMItem(instrSet, offset, length, std::move(name)), bank(theBank), instrNum(theInstrNum),
      parInstrSet(instrSet), reverb(reverb) {
}

void VGMInstr::SetBank(uint32_t bankNum) {
  bank = bankNum;
}

void VGMInstr::SetInstrNum(uint32_t theInstrNum) {
  instrNum = theInstrNum;
}

VGMRgn *VGMInstr::AddRgn(VGMRgn *rgn) {
  m_regions.emplace_back(rgn);
  addChild(rgn);
  return rgn;
}

VGMRgn *VGMInstr::AddRgn(uint32_t offset, uint32_t length, int sampNum, uint8_t keyLow,
                         uint8_t keyHigh, uint8_t velLow, uint8_t velHigh) {
  VGMRgn *newRgn = new VGMRgn(this, offset, length, keyLow, keyHigh, velLow, velHigh, sampNum);
  m_regions.emplace_back(newRgn);
  addChild(newRgn);
  return newRgn;
}

bool VGMInstr::LoadInstr() {
  return true;
}
