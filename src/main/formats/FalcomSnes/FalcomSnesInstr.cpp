/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "FalcomSnesInstr.h"

#include <spdlog/fmt/fmt.h>
#include "SNESDSP.h"

// ******************
// FalcomSnesInstrSet
// ******************

FalcomSnesInstrSet::FalcomSnesInstrSet(RawFile *file,
                                       FalcomSnesVersion ver,
                                       uint32_t offset,
                                       uint32_t addrSampToInstrTable,
                                       uint32_t spcDirAddr,
                                       const std::map<uint8_t, uint16_t> &instrADSRHints,
                                       const std::string &name) :
    VGMInstrSet(FalcomSnesFormat::name, file, offset, 0, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrSampToInstrTable(addrSampToInstrTable),
    instrADSRHints(instrADSRHints) {}

FalcomSnesInstrSet::~FalcomSnesInstrSet() {}

bool FalcomSnesInstrSet::parseHeader() {
  return true;
}

bool FalcomSnesInstrSet::parseInstrPointers() {
  constexpr uint16_t kInstrItemSize = 5;

  usedSRCNs.clear();
  for (int instr = 0; instr < 255 / kInstrItemSize; instr++) {
    uint32_t addrInstrHeader = dwOffset + (kInstrItemSize * instr);
    if (addrInstrHeader + kInstrItemSize > 0x10000) {
      break;
    }

    // determine the sample number
    uint8_t srcn;
    bool srcnDetermined = false;
    for (srcn = 0; srcn < 32; srcn++) {
      if (addrSampToInstrTable + srcn >= 0x10000) {
        break;
      }
      uint8_t value = readByte(addrSampToInstrTable + srcn);
      if (value == instr) {
        srcnDetermined = true;
        break;
      }
    }
    if (!srcnDetermined) {
      continue;
    }

    uint32_t offDirEnt = spcDirAddr + (srcn * 4);
    if (offDirEnt + 4 > 0x10000) {
      break;
    }

    uint16_t addrSampStart = readShort(offDirEnt);
    uint16_t addrLoopStart = readShort(offDirEnt + 2);

    if (addrSampStart == 0x0000 && addrLoopStart == 0x0000) {
      continue;
    }

    if (addrSampStart < offDirEnt + 4) {
      continue;
    }

    std::vector<uint8_t>::iterator itrSRCN = std::ranges::find(usedSRCNs, srcn);
    if (itrSRCN == usedSRCNs.end()) {
      usedSRCNs.push_back(srcn);
    }

    auto instrName = fmt::format("Instrument {}", instr);
    FalcomSnesInstr *newInstr = new FalcomSnesInstr(
      this, version, addrInstrHeader, instr >> 7, instr & 0x7f, srcn,
      spcDirAddr, instrName);
    aInstrs.push_back(newInstr);
  }
  if (aInstrs.size() == 0) {
    return false;
  }

  std::ranges::sort(usedSRCNs);
  SNESSampColl *newSampColl = new SNESSampColl(FalcomSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// ***************
// FalcomSnesInstr
// ***************

FalcomSnesInstr::FalcomSnesInstr(VGMInstrSet *instrSet,
                                 FalcomSnesVersion ver,
                                 uint32_t offset,
                                 uint32_t theBank,
                                 uint32_t theInstrNum,
                                 uint8_t srcn,
                                 uint32_t spcDirAddr,
                                 const std::string &name) :
    VGMInstr(instrSet, offset, 5, theBank, theInstrNum, name), version(ver),
    srcn(srcn),
    spcDirAddr(spcDirAddr) {}

FalcomSnesInstr::~FalcomSnesInstr() {}

bool FalcomSnesInstr::loadInstr() {
  uint32_t offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  uint16_t addrSampStart = readShort(offDirEnt);

  FalcomSnesRgn *rgn = new FalcomSnesRgn(this, version, dwOffset, srcn);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  return true;
}

// *************
// FalcomSnesRgn
// *************

FalcomSnesRgn::FalcomSnesRgn(FalcomSnesInstr *instr,
                             FalcomSnesVersion ver,
                             uint32_t offset,
                             uint8_t srcn) :
    VGMRgn(instr, offset, 5), version(ver) {
  uint8_t adsr1 = readByte(offset);
  uint8_t adsr2 = readByte(offset + 1);
  int16_t pitch_scale = getShortBE(offset + 3);

  // override ADSR
  //if (parInstrSet->instrADSRHints.count(instr->instrNum) != 0) {
  //  uint16_t adsr = parInstrSet->instrADSRHints[instr->instrNum];
  //  if (adsr != 0) {
  //    adsr1 = adsr & 0xff;
  //    adsr2 = (adsr >> 8) & 0xff;
  //  }
  //}

  constexpr double pitch_fixer = 4286.0 / 4096.0;
  double coarse_tuning;
  double fine_tuning = modf((log(pitch_scale * pitch_fixer / 256.0) / log(2.0)) * 12.0, &coarse_tuning);

  // normalize
  if (fine_tuning >= 0.5) {
    coarse_tuning += 1.0;
    fine_tuning -= 1.0;
  }
  else if (fine_tuning <= -0.5) {
    coarse_tuning -= 1.0;
    fine_tuning += 1.0;
  }

  sampNum = srcn;
  addChild(offset, 1, "ADSR1");
  addChild(offset + 1, 1, "ADSR2");
  addUnityKey(96 - static_cast<int>(coarse_tuning), offset + 3, 1);
  addFineTune(static_cast<int16_t>(fine_tuning * 100.0), offset + 4, 1);
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, 0);
}

FalcomSnesRgn::~FalcomSnesRgn() {}

bool FalcomSnesRgn::loadRgn() {
  return true;
}
