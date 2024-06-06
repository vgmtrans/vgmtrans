/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ChunSnesInstr.h"

#include <spdlog/fmt/fmt.h>
#include "SNESDSP.h"

// ****************
// ChunSnesInstrSet
// ****************

ChunSnesInstrSet::ChunSnesInstrSet(RawFile *file,
                                   ChunSnesVersion ver,
                                   uint16_t addrInstrSet,
                                   uint16_t addrSampNumTable,
                                   uint16_t addrSampleTable,
                                   uint32_t spcDirAddr,
                                   const std::string &name) :
    VGMInstrSet(ChunSnesFormat::name, file, addrInstrSet, 0, name), version(ver),
    addrSampNumTable(addrSampNumTable),
    addrSampleTable(addrSampleTable),
    spcDirAddr(spcDirAddr) {
}

ChunSnesInstrSet::~ChunSnesInstrSet() {
}

bool ChunSnesInstrSet::GetHeaderInfo() {
  uint32_t curOffset = dwOffset;
  if (curOffset + 2 > 0x10000) {
    return false;
  }

  unsigned int nNumInstrs = GetByte(curOffset);
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

bool ChunSnesInstrSet::GetInstrPointers() {
  usedSRCNs.clear();

  uint32_t curOffset = dwOffset;
  unsigned int nNumInstrs = GetByte(curOffset);
  if (version == CHUNSNES_SUMMER) {
    curOffset += 1;
  } else {  // CHUNSNES_WINTER
    curOffset += 2;
  }

  for (unsigned int instrNum = 0; instrNum < nNumInstrs; instrNum++) {
    auto instrName = fmt::format("Instrument {}", instrNum + 1);
    addChild(curOffset, 1, instrName);

    uint8_t globalInstrNum = GetByte(curOffset);
    curOffset++;

    uint32_t addrInstr = addrSampNumTable + globalInstrNum;
    if (addrInstr > 0x10000) {
      return false;
    }

    uint8_t srcn = GetByte(addrInstr);
    if (srcn != 0xff) {
      std::vector<uint8_t>::iterator itrSRCN = std::ranges::find(usedSRCNs, srcn);
      if (itrSRCN == usedSRCNs.end()) {
        usedSRCNs.push_back(srcn);
      }

      if (addrInstr < dwOffset) {
        dwOffset = addrInstr;
      }

      ChunSnesInstr *newInstr = new ChunSnesInstr(this, version, instrNum, addrInstr,
        addrSampleTable, spcDirAddr, instrName);
      aInstrs.push_back(newInstr);
    }
  }

  if (aInstrs.size() == 0) {
    return false;
  }

  std::ranges::sort(usedSRCNs);
  SNESSampColl *newSampColl = new SNESSampColl(ChunSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->LoadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// *************
// ChunSnesInstr
// *************

ChunSnesInstr::ChunSnesInstr(VGMInstrSet *instrSet,
                             ChunSnesVersion ver,
                             uint8_t theInstrNum,
                             uint16_t addrInstr,
                             uint16_t addrSampleTable,
                             uint32_t spcDirAddr,
                             const std::string &name) :
    VGMInstr(instrSet, addrInstr, 0, 0, theInstrNum, name), version(ver),
    addrSampleTable(addrSampleTable),
    spcDirAddr(spcDirAddr) {}

ChunSnesInstr::~ChunSnesInstr() {}

bool ChunSnesInstr::LoadInstr() {
  uint8_t srcn = GetByte(dwOffset);
  addChild(dwOffset, 1, "Sample Number");
  if (srcn == 0xff) {
    return false;
  }

  uint32_t offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  uint16_t addrSampStart = GetShort(offDirEnt);

  uint32_t addrRgn = addrSampleTable + (srcn * 8);
  if (addrRgn + 8 > 0x10000) {
    return false;
  }

  ChunSnesRgn *rgn = new ChunSnesRgn(this, version, srcn, addrRgn, spcDirAddr);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  AddRgn(rgn);

  SetGuessedLength();
  return true;
}

// ***********
// ChunSnesRgn
// ***********

ChunSnesRgn::ChunSnesRgn(ChunSnesInstr *instr, ChunSnesVersion ver, uint8_t srcn, uint16_t addrRgn, uint32_t spcDirAddr) :
    VGMRgn(instr, addrRgn, 8),
    version(ver) {
  AddUnknown(dwOffset, 2);
  addChild(dwOffset + 2, 1, "ADSR(1)");
  addChild(dwOffset + 3, 1, "ADSR(2)");
  addChild(dwOffset + 4, 1, "GAIN");
  addChild(dwOffset + 5, 2, "Tuning");
  AddUnknown(dwOffset + 7, 1);

  const uint8_t adsr1 = GetByte(dwOffset + 2);
  const uint8_t adsr2 = GetByte(dwOffset + 3);
  const uint8_t gain = GetByte(dwOffset + 4);
  const int16_t pitch_scale = GetShortBE(dwOffset + 5);

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
  fineTune = static_cast<int16_t>(fine_tuning * 100.0);
  SNESConvADSR<VGMRgn>(this, adsr1, adsr2, gain);

  // use ADSR sustain for release rate
  uint8_t sr_release = 0x19; // default release rate
  ConvertSNESADSR(adsr1, (adsr2 & 0xe0) | sr_release, gain, 0x7ff, nullptr,
    nullptr, nullptr, &this->release_time, nullptr);
}

ChunSnesRgn::~ChunSnesRgn() {}

bool ChunSnesRgn::LoadRgn() {
  return true;
}
