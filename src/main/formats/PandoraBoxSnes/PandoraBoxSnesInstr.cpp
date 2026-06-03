/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "PandoraBoxSnesInstr.h"

#include "base/Types.h"
#include "SNESDSP.h"

#include <spdlog/fmt/fmt.h>

// **********************
// PandoraBoxSnesInstrSet
// **********************

PandoraBoxSnesInstrSet::PandoraBoxSnesInstrSet(RawFile *file,
                                               PandoraBoxSnesVersion ver,
                                               u32 spcDirAddr,
                                               u16 addrLocalInstrTable,
                                               u16 addrGlobalInstrTable,
                                               u8 globalInstrumentCount,
                                               const std::map<u8, u16> &instrADSRHints,
                                               const std::string &name) :
    VGMInstrSet(PandoraBoxSnesFormat::name, file, addrLocalInstrTable, 0, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrLocalInstrTable(addrLocalInstrTable),
    addrGlobalInstrTable(addrGlobalInstrTable),
    globalInstrumentCount(globalInstrumentCount),
    instrADSRHints(instrADSRHints) {
}

PandoraBoxSnesInstrSet::~PandoraBoxSnesInstrSet() {
}

bool PandoraBoxSnesInstrSet::parseHeader() {
  if (globalInstrumentCount == 0) {
    return false;
  }

  if (addrGlobalInstrTable + globalInstrumentCount > 0x10000) {
    return false;
  }

  // read global instrument table into vector
  for (u8 globalInstrNum = 0; globalInstrNum < globalInstrumentCount; globalInstrNum++) {
    globalInstrTable.push_back(readByte(addrGlobalInstrTable + globalInstrNum));
  }

  return true;
}

bool PandoraBoxSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();

  for (u8 instrNum = 0; instrNum <= 0x7f; instrNum++) {
    u32 addrLocalInstrItem = addrLocalInstrTable + instrNum;
    if (addrLocalInstrItem >= 0x10000) {
      break;
    }

    u8 globalInstrNum = readByte(addrLocalInstrItem);

    // search instrument number and get SRCN
    // note: actual engine do backward search, but I do not care
    auto iterInstrItem = std::find(globalInstrTable.begin(), globalInstrTable.end(), globalInstrNum);
    if (iterInstrItem == globalInstrTable.end()) {
      // out of range, perhaps?
      // actual music engine will use SRCN 0 for such case
      break;
    }
    u8 srcn = std::distance(globalInstrTable.begin(), iterInstrItem);

    u32 addrDIRentry = spcDirAddr + (srcn * 4);
    if (!SNESSampColl::isValidSampleDir(rawFile(), addrDIRentry, true)) {
      break;
    }

    std::vector<u8>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
    if (itrSRCN == usedSRCNs.end()) {
      usedSRCNs.push_back(srcn);
    }

    u16 adsr = 0x8fe0;
    if (instrADSRHints.count(srcn)) {
      adsr = instrADSRHints[srcn];
    }

    PandoraBoxSnesInstr *newInstr = new PandoraBoxSnesInstr(
      this, version, addrLocalInstrItem, instrNum, srcn, spcDirAddr, adsr,
      fmt::format("Instrument {}", srcn));
    aInstrs.push_back(newInstr);
  }

  if (aInstrs.size() == 0) {
    return false;
  }

  std::sort(usedSRCNs.begin(), usedSRCNs.end());
  SNESSampColl *newSampColl = new SNESSampColl(PandoraBoxSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// *******************
// PandoraBoxSnesInstr
// *******************

PandoraBoxSnesInstr::PandoraBoxSnesInstr(VGMInstrSet *instrSet,
                                         PandoraBoxSnesVersion ver,
                                         u32 offset,
                                         u8 theInstrNum,
                                         u8 srcn,
                                         u32 spcDirAddr,
                                         u16 adsr,
                                         const std::string &name) :
    VGMInstr(instrSet, offset, 1, 0, theInstrNum, name), version(ver),
    spcDirAddr(spcDirAddr),
    srcn(srcn),
    adsr(adsr) {
}

PandoraBoxSnesInstr::~PandoraBoxSnesInstr() {
}

bool PandoraBoxSnesInstr::loadInstr() {
  u32 offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  u16 addrSampStart = readShort(offDirEnt);

  PandoraBoxSnesRgn *rgn = new PandoraBoxSnesRgn(this, version, offset(), srcn, spcDirAddr, adsr);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  setGuessedLength();
  return true;
}

// *****************
// PandoraBoxSnesRgn
// *****************

PandoraBoxSnesRgn::PandoraBoxSnesRgn(PandoraBoxSnesInstr *instr,
                                     PandoraBoxSnesVersion ver,
                                     u32 offset,
                                     u8 srcn,
                                     u32 spcDirAddr,
                                     u16 adsr) :
    VGMRgn(instr, offset, 1),
    version(ver) {
  u8 adsr1 = adsr >> 8;
  u8 adsr2 = adsr & 0xff;

  addChild(offset, 1, "Global Instrument #");

  sampNum = srcn;
  unityKey = 45; // o3a = $1000
  fineTune = 0;
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, 0x7f);
}

PandoraBoxSnesRgn::~PandoraBoxSnesRgn() {
}

bool PandoraBoxSnesRgn::loadRgn() {
  return true;
}
