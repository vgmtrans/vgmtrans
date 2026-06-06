/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ChunSnesInstr.h"

#include "base/Types.h"
#include "SNESDSP.h"

#include <spdlog/fmt/fmt.h>

// ****************
// ChunSnesInstrSet
// ****************

ChunSnesInstrSet::ChunSnesInstrSet(RawFile *file,
                                   ChunSnesVersion ver,
                                   u16 addrInstrSet,
                                   u16 addrSampNumTable,
                                   u16 addrSampleTable,
                                   u32 spcDirAddr,
                                   const std::string &name) :
    VGMInstrSet(ChunSnesFormat::name, file, addrInstrSet, 0, name), version(ver),
    addrSampNumTable(addrSampNumTable),
    addrSampleTable(addrSampleTable),
    spcDirAddr(spcDirAddr) {
}

ChunSnesInstrSet::~ChunSnesInstrSet() {
}

bool ChunSnesInstrSet::parseHeader() {
  u32 curOffset = offset();
  if (curOffset + 2 > 0x10000) {
    return false;
  }

  unsigned int nNumInstrs = readByte(curOffset);
  addChild(curOffset, 1, "Number of Instruments");
  curOffset++;

  if (version != CHUNSNES_SUMMER) { // CHUNSNES_WINTER
    addUnknownChild(curOffset, 1);
    curOffset++;
  }

  if (curOffset + nNumInstrs > 0x10000) {
    return false;
  }

  return true;
}

bool ChunSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();

  u32 curOffset = offset();
  unsigned int nNumInstrs = readByte(curOffset);
  if (version == CHUNSNES_SUMMER) {
    curOffset += 1;
  } else {  // CHUNSNES_WINTER
    curOffset += 2;
  }

  for (unsigned int instrNum = 0; instrNum < nNumInstrs; instrNum++) {
    auto instrName = fmt::format("Instrument {}", instrNum + 1);
    addChild(curOffset, 1, instrName);

    u8 globalInstrNum = readByte(curOffset);
    curOffset++;

    u32 addrInstr = addrSampNumTable + globalInstrNum;
    if (addrInstr > 0x10000) {
      return false;
    }

    u8 srcn = readByte(addrInstr);
    if (srcn != 0xff) {
      std::vector<u8>::iterator itrSRCN = std::ranges::find(usedSRCNs, srcn);
      if (itrSRCN == usedSRCNs.end()) {
        usedSRCNs.push_back(srcn);
      }

      if (addrInstr < offset()) {
        setOffset(addrInstr);
      }

      addInstr<ChunSnesInstr>(this, version, instrNum, addrInstr,
                                  addrSampleTable, spcDirAddr, instrName);
    }
  }

  if (!hasInstrs()) {
    return false;
  }

  std::ranges::sort(usedSRCNs);
  if (!addDiscoveredFile<SNESSampColl>(ChunSnesFormat::name, rawFile(), spcDirAddr, usedSRCNs)) {
    return false;
  }

  return true;
}

// *************
// ChunSnesInstr
// *************

ChunSnesInstr::ChunSnesInstr(VGMInstrSet *instrSet,
                             ChunSnesVersion ver,
                             u8 instrNum,
                             u16 addrInstr,
                             u16 addrSampleTable,
                             u32 spcDirAddr,
                             const std::string &name) :
    VGMInstr(instrSet, addrInstr, 0, 0, instrNum, name), version(ver),
    addrSampleTable(addrSampleTable),
    spcDirAddr(spcDirAddr) {}

ChunSnesInstr::~ChunSnesInstr() {}

bool ChunSnesInstr::loadInstr() {
  u8 srcn = readByte(offset());
  addChild(offset(), 1, "Sample Number");
  if (srcn == 0xff) {
    return false;
  }

  u32 offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  u16 addrSampStart = readShort(offDirEnt);

  u32 addrRgn = addrSampleTable + (srcn * 8);
  if (addrRgn + 8 > 0x10000) {
    return false;
  }

  ChunSnesRgn *rgn = addRgn<ChunSnesRgn>(this, version, srcn, addrRgn, spcDirAddr);
  rgn->sampOffset = addrSampStart - spcDirAddr;

  setGuessedLength();
  return true;
}

// ***********
// ChunSnesRgn
// ***********

ChunSnesRgn::ChunSnesRgn(ChunSnesInstr *instr, ChunSnesVersion ver, u8 srcn, u16 addrRgn, u32 spcDirAddr) :
    VGMRgn(instr, addrRgn, 8),
    version(ver) {
  addUnknown(offset(), 2);
  addChild(offset() + 2, 1, "ADSR(1)");
  addChild(offset() + 3, 1, "ADSR(2)");
  addChild(offset() + 4, 1, "GAIN");
  addChild(offset() + 5, 2, "Tuning");
  addUnknown(offset() + 7, 1);

  const u8 adsr1 = readByte(offset() + 2);
  const u8 adsr2 = readByte(offset() + 3);
  const u8 gain = readByte(offset() + 4);
  const s16 pitch_scale = getShortBE(offset() + 5);

  const double pitch_fixer = (version == CHUNSNES_SUMMER) ? (7902.0 / 8192.0) : (7938.0 / 8192.0); // from pitch table
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
  const int baseKey = (version == CHUNSNES_SUMMER) ? 95 : 119;
  unityKey = baseKey - static_cast<int>(coarse_tuning);
  fineTune = static_cast<s16>(fine_tuning * 100.0);
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);

  // use ADSR sustain for release rate
  u8 sr_release = 0x19; // default release rate
  convertSNESADSR(adsr1, (adsr2 & 0xe0) | sr_release, gain, 0x7ff, nullptr,
    nullptr, nullptr, &this->release_time, nullptr);
}

ChunSnesRgn::~ChunSnesRgn() {}

bool ChunSnesRgn::loadRgn() {
  return true;
}
