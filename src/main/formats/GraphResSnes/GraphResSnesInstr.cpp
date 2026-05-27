/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "Types.h"
#include "GraphResSnesInstr.h"

#include <spdlog/fmt/fmt.h>
#include "SNESDSP.h"

// ********************
// GraphResSnesInstrSet
// ********************

GraphResSnesInstrSet::GraphResSnesInstrSet(RawFile *file,
                                           GraphResSnesVersion ver,
                                           u32 spcDirAddr,
                                           const std::map<u8, u16> &instrADSRHints,
                                           const std::string &name) :
    VGMInstrSet(GraphResSnesFormat::name, file, spcDirAddr, 0, name), version(ver),
    spcDirAddr(spcDirAddr),
    instrADSRHints(instrADSRHints) {
}

GraphResSnesInstrSet::~GraphResSnesInstrSet() {
}

bool GraphResSnesInstrSet::parseHeader() {
  return true;
}

bool GraphResSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();

  u16 addrSampEntryMax = 0xffff;
  for (u8 srcn = 0; srcn <= 0x7f; srcn++) {
    u32 addrDIRentry = spcDirAddr + (srcn * 4);
    if (!SNESSampColl::isValidSampleDir(rawFile(), addrDIRentry, true)) {
      continue;
    }

    if (addrDIRentry >= addrSampEntryMax) {
      break;
    }

    u16 addrSampStart = readShort(addrDIRentry);
    if (addrSampStart < addrDIRentry + 4) {
      break;
    }

    if (addrSampStart == 0) {
      break;
    }

    if (addrSampStart < addrSampEntryMax) {
      addrSampEntryMax = addrSampStart;
    }

    usedSRCNs.push_back(srcn);

    u16 adsr = 0x8fe0;
    if (instrADSRHints.contains(srcn)) {
      adsr = instrADSRHints[srcn];
    }

    GraphResSnesInstr *newInstr = new GraphResSnesInstr(
      this, version, srcn, spcDirAddr, adsr, fmt::format("Instrument {}", srcn));
    aInstrs.push_back(newInstr);
  }

  if (aInstrs.size() == 0) {
    return false;
  }

  std::ranges::sort(usedSRCNs);
  SNESSampColl *newSampColl = new SNESSampColl(GraphResSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// *****************
// GraphResSnesInstr
// *****************

GraphResSnesInstr::GraphResSnesInstr(VGMInstrSet *instrSet,
                                     GraphResSnesVersion ver,
                                     u8 srcn,
                                     u32 spcDirAddr,
                                     u16 adsr,
                                     const std::string &name) :
    VGMInstr(instrSet, spcDirAddr + srcn * 4, 4, 0, srcn, name), version(ver),
    spcDirAddr(spcDirAddr),
    adsr(adsr) {
}

GraphResSnesInstr::~GraphResSnesInstr() {
}

bool GraphResSnesInstr::loadInstr() {
  u32 offDirEnt = spcDirAddr + (instrNum * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  u16 addrSampStart = readShort(offDirEnt);

  GraphResSnesRgn *rgn = new GraphResSnesRgn(this, version, instrNum, spcDirAddr, adsr);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  setGuessedLength();
  return true;
}

// ***************
// GraphResSnesRgn
// ***************

GraphResSnesRgn::GraphResSnesRgn(GraphResSnesInstr *instr,
                                 GraphResSnesVersion ver,
                                 u8 srcn,
                                 u32 spcDirAddr,
                                 u16 adsr) :
    VGMRgn(instr, spcDirAddr + srcn * 4, 4),
    version(ver) {
  u8 adsr1 = adsr >> 8;
  u8 adsr2 = adsr & 0xff;

  u32 offDirEnt = spcDirAddr + srcn * 4;
  addChild(offDirEnt, 2, "SA");
  addChild(offDirEnt + 2, 2, "LSA");

  sampNum = srcn;
  unityKey = 57; // o4a = $1000
  fineTune = 0;
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, 0);
}

GraphResSnesRgn::~GraphResSnesRgn() {
}

bool GraphResSnesRgn::loadRgn() {
  return true;
}
