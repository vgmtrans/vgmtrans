/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "util/types.h"
#include "HeartBeatSnesInstr.h"
#include <spdlog/fmt/fmt.h>
#include "SNESDSP.h"

// *********************
// HeartBeatSnesInstrSet
// *********************

HeartBeatSnesInstrSet::HeartBeatSnesInstrSet(RawFile *file,
                                             HeartBeatSnesVersion ver,
                                             u32 offset,
                                             u32 length,
                                             u16 addrSRCNTable,
                                             u8 songIndex,
                                             u32 spcDirAddr,
                                             const std::string &name) :
    VGMInstrSet(HeartBeatSnesFormat::name, file, offset, length, name), version(ver),
    addrSRCNTable(addrSRCNTable),
    songIndex(songIndex),
    spcDirAddr(spcDirAddr) {
}

HeartBeatSnesInstrSet::~HeartBeatSnesInstrSet() {
}

bool HeartBeatSnesInstrSet::parseHeader() {
  return true;
}

bool HeartBeatSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();

  u8 nNumInstrs = length() / 6;
  if (nNumInstrs == 0) {
    return false;
  }

  for (u8 instrNum = 0; instrNum < nNumInstrs; instrNum++) {
    u32 addrInstrHeader = offset() + (instrNum * 6);
    if (addrInstrHeader + 6 > 0x10000) {
      break;
    }

    u8 sampleIndex = readByte(addrInstrHeader) + (songIndex * 0x10);
    if (addrSRCNTable + sampleIndex + 1 > 0x10000) {
      break;
    }

    u8 srcn = readByte(addrSRCNTable + sampleIndex);

    u32 offDirEnt = spcDirAddr + (srcn * 4);
    u16 addrSampStart = readShort(offDirEnt);
    if (addrSampStart < offDirEnt + 4) {
      continue;
    }

    if (!SNESSampColl::isValidSampleDir(rawFile(), offDirEnt, true)) {
      // safety logic
      setLength(instrNum * 6);
      break;
    }

    std::vector<u8>::iterator itrSRCN = std::ranges::find(usedSRCNs, srcn);
    if (itrSRCN == usedSRCNs.end()) {
      usedSRCNs.push_back(srcn);
    }

    HeartBeatSnesInstr *newInstr = new HeartBeatSnesInstr(
      this, version, addrInstrHeader, instrNum >> 7, instrNum & 0x7f, addrSRCNTable,
      songIndex, spcDirAddr, fmt::format("Instrument {}", instrNum));
    aInstrs.push_back(newInstr);
  }

  if (aInstrs.size() == 0) {
    return false;
  }

  std::ranges::sort(usedSRCNs);
  SNESSampColl *newSampColl = new SNESSampColl(HeartBeatSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// ******************
// HeartBeatSnesInstr
// ******************

HeartBeatSnesInstr::HeartBeatSnesInstr(VGMInstrSet *instrSet,
                                       HeartBeatSnesVersion ver,
                                       u32 offset,
                                       u32 theBank,
                                       u32 theInstrNum,
                                       u16 addrSRCNTable,
                                       u8 songIndex,
                                       u32 spcDirAddr,
                                       const std::string &name) :
    VGMInstr(instrSet, offset, 6, theBank, theInstrNum, name), version(ver),
    addrSRCNTable(addrSRCNTable),
    songIndex(songIndex),
    spcDirAddr(spcDirAddr) {
}

HeartBeatSnesInstr::~HeartBeatSnesInstr() {
}

bool HeartBeatSnesInstr::loadInstr() {
  u8 sampleIndex = readByte(offset()) + (songIndex * 0x10);
  if (addrSRCNTable + sampleIndex + 1 > 0x10000) {
    return false;
  }

  u8 srcn = readByte(addrSRCNTable + sampleIndex);

  u32 offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  u16 addrSampStart = readShort(offDirEnt);

  HeartBeatSnesRgn *rgn = new HeartBeatSnesRgn(this, version, offset());
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  return true;
}

// ****************
// HeartBeatSnesRgn
// ****************

HeartBeatSnesRgn::HeartBeatSnesRgn(HeartBeatSnesInstr *instr, HeartBeatSnesVersion ver, u32 offset) :
    VGMRgn(instr, offset, 6), version(ver) {
  u8 srcn = readByte(offset);
  u8 adsr1 = readByte(offset + 1);
  u8 adsr2 = readByte(offset + 2);
  u8 gain = readByte(offset + 3);
  s16 pitch_scale = getShortBE(offset + 4);

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

  addSampNum(srcn, offset, 1);
  addChild(offset + 1, 1, "ADSR1");
  addChild(offset + 2, 1, "ADSR2");
  addChild(offset + 3, 1, "GAIN");
  addUnityKey(72 - static_cast<int>(coarse_tuning), offset + 4, 1);
  addFineTune(static_cast<s16>(fine_tuning * 100.0), offset + 5, 1);
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);

  // use ADSR sustain for release rate
  // actual music engine sets sustain rate to release rate, it's useless,
  // so here I put a random value commonly used
  // TODO: obtain proper release rate from sequence
  u8 sr_release = 0x1c;
  convertSNESADSR(adsr1, (adsr2 & 0xe0) | sr_release, gain, 0x7ff, nullptr,
    nullptr, nullptr, &this->release_time, nullptr);
}

HeartBeatSnesRgn::~HeartBeatSnesRgn() {}

bool HeartBeatSnesRgn::loadRgn() {
  return true;
}
