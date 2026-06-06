/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "FalcomSnesInstr.h"

#include "base/Types.h"
#include "SNESDSP.h"

#include <spdlog/fmt/fmt.h>

// ******************
// FalcomSnesInstrSet
// ******************

FalcomSnesInstrSet::FalcomSnesInstrSet(RawFile *file,
                                       FalcomSnesVersion ver,
                                       u32 offset,
                                       u32 addrSampToInstrTable,
                                       u32 spcDirAddr,
                                       const std::map<u8, u16> &instrADSRHints,
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
  constexpr u16 kInstrItemSize = 5;

  usedSRCNs.clear();
  for (int instr = 0; instr < 255 / kInstrItemSize; instr++) {
    u32 addrInstrHeader = offset() + (kInstrItemSize * instr);
    if (addrInstrHeader + kInstrItemSize > 0x10000) {
      break;
    }

    // determine the sample number
    u8 srcn;
    bool srcnDetermined = false;
    for (srcn = 0; srcn < 32; srcn++) {
      if (addrSampToInstrTable + srcn >= 0x10000) {
        break;
      }
      u8 value = readByte(addrSampToInstrTable + srcn);
      if (value == instr) {
        srcnDetermined = true;
        break;
      }
    }
    if (!srcnDetermined) {
      continue;
    }

    u32 offDirEnt = spcDirAddr + (srcn * 4);
    if (offDirEnt + 4 > 0x10000) {
      break;
    }

    u16 addrSampStart = readShort(offDirEnt);
    u16 addrLoopStart = readShort(offDirEnt + 2);

    if (addrSampStart == 0x0000 && addrLoopStart == 0x0000) {
      continue;
    }

    if (addrSampStart < offDirEnt + 4) {
      continue;
    }

    std::vector<u8>::iterator itrSRCN = std::ranges::find(usedSRCNs, srcn);
    if (itrSRCN == usedSRCNs.end()) {
      usedSRCNs.push_back(srcn);
    }

    auto instrName = fmt::format("Instrument {}", instr);
    addInstr<FalcomSnesInstr>(
      this, version, addrInstrHeader, instr >> 7, instr & 0x7f, srcn,
      spcDirAddr, instrName);
  }
  if (!hasInstrs()) {
    return false;
  }

  std::ranges::sort(usedSRCNs);
  if (!addDiscoveredFile<SNESSampColl>(FalcomSnesFormat::name, rawFile(), spcDirAddr, usedSRCNs)) {
    return false;
  }

  return true;
}

// ***************
// FalcomSnesInstr
// ***************

FalcomSnesInstr::FalcomSnesInstr(VGMInstrSet *instrSet,
                                 FalcomSnesVersion ver,
                                 u32 offset,
                                 u32 bank,
                                 u32 instrNum,
                                 u8 srcn,
                                 u32 spcDirAddr,
                                 const std::string &name) :
    VGMInstr(instrSet, offset, 5, bank, instrNum, name), version(ver),
    srcn(srcn),
    spcDirAddr(spcDirAddr) {}

FalcomSnesInstr::~FalcomSnesInstr() {}

bool FalcomSnesInstr::loadInstr() {
  u32 offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  u16 addrSampStart = readShort(offDirEnt);

  FalcomSnesRgn *rgn = addRgn<FalcomSnesRgn>(this, version, offset(), srcn);
  rgn->sampOffset = addrSampStart - spcDirAddr;

  return true;
}

// *************
// FalcomSnesRgn
// *************

FalcomSnesRgn::FalcomSnesRgn(FalcomSnesInstr *instr,
                             FalcomSnesVersion ver,
                             u32 offset,
                             u8 srcn) :
    VGMRgn(instr, offset, 5), version(ver) {
  u8 adsr1 = readByte(offset);
  u8 adsr2 = readByte(offset + 1);
  s16 pitch_scale = getShortBE(offset + 3);

  // override ADSR
  //if (parInstrSet->instrADSRHints.count(instr->instrNum) != 0) {
  //  u16 adsr = parInstrSet->instrADSRHints[instr->instrNum];
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
  addFineTune(static_cast<s16>(fine_tuning * 100.0), offset + 4, 1);
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, 0);
}

FalcomSnesRgn::~FalcomSnesRgn() {}

bool FalcomSnesRgn::loadRgn() {
  return true;
}
