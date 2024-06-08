/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PrismSnesInstr.h"
#include "SNESDSP.h"
#include <spdlog/fmt/fmt.h>

// *****************
// PrismSnesInstrSet
// *****************

PrismSnesInstrSet::PrismSnesInstrSet(RawFile *file,
                                     PrismSnesVersion ver,
                                     uint32_t spcDirAddr,
                                     uint16_t addrADSR1Table,
                                     uint16_t addrADSR2Table,
                                     uint16_t addrTuningTableHigh,
                                     uint16_t addrTuningTableLow,
                                     const std::string &name) :
    VGMInstrSet(PrismSnesFormat::name, file, addrADSR1Table, 0, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrADSR1Table(addrADSR1Table),
    addrADSR2Table(addrADSR2Table),
    addrTuningTableHigh(addrTuningTableHigh),
    addrTuningTableLow(addrTuningTableLow) {}

PrismSnesInstrSet::~PrismSnesInstrSet() {}

bool PrismSnesInstrSet::parseHeader() {
  return true;
}

bool PrismSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();
  for (uint16_t srcn16 = 0; srcn16 < 0x100; srcn16++) {
    uint8_t srcn = (uint8_t)srcn16;

    uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
    if (!SNESSampColl::isValidSampleDir(rawFile(), addrDIRentry, true)) {
      continue;
    }

    uint16_t addrSampStart = readShort(addrDIRentry);
    if (addrSampStart < spcDirAddr) {
      continue;
    }

    uint32_t ofsADSR1Entry;
    ofsADSR1Entry = addrADSR1Table + srcn;
    if (ofsADSR1Entry + 1 > 0x10000) {
      break;
    }

    uint32_t ofsADSR2Entry;
    ofsADSR2Entry = addrADSR2Table + srcn;
    if (ofsADSR2Entry + 1 > 0x10000) {
      break;
    }

    uint32_t ofsTuningEntryHigh;
    ofsTuningEntryHigh = addrTuningTableHigh + srcn;
    if (ofsTuningEntryHigh + 1 > 0x10000) {
      break;
    }

    uint32_t ofsTuningEntryLow;
    ofsTuningEntryLow = addrTuningTableLow + srcn;
    if (ofsTuningEntryLow + 1 > 0x10000) {
      break;
    }

    usedSRCNs.push_back(srcn);

    PrismSnesInstr *newInstr = new PrismSnesInstr(
      this, version, srcn, spcDirAddr, ofsADSR1Entry, ofsADSR2Entry, ofsTuningEntryHigh,
      ofsTuningEntryLow, fmt::format("Instrument: {:#x}", srcn));
    aInstrs.push_back(newInstr);
  }

  if (aInstrs.size() == 0) {
    return false;
  }

  std::sort(usedSRCNs.begin(), usedSRCNs.end());
  SNESSampColl *newSampColl = new SNESSampColl(PrismSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// **************
// PrismSnesInstr
// **************

PrismSnesInstr::PrismSnesInstr(VGMInstrSet *instrSet,
                               PrismSnesVersion ver,
                               uint8_t srcn,
                               uint32_t spcDirAddr,
                               uint16_t addrADSR1Entry,
                               uint16_t addrADSR2Entry,
                               uint16_t addrTuningEntryHigh,
                               uint16_t addrTuningEntryLow,
                               const std::string &name) :
    VGMInstr(instrSet, addrADSR1Entry, 0, srcn >> 7, srcn & 0x7f, name), version(ver),
    srcn(srcn),
    spcDirAddr(spcDirAddr),
    addrADSR1Entry(addrADSR1Entry),
    addrADSR2Entry(addrADSR2Entry),
    addrTuningEntryHigh(addrTuningEntryHigh),
    addrTuningEntryLow(addrTuningEntryLow) {
}

PrismSnesInstr::~PrismSnesInstr() {
}

bool PrismSnesInstr::loadInstr() {
  uint32_t offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  uint16_t addrSampStart = readShort(offDirEnt);

  PrismSnesRgn *rgn = new PrismSnesRgn(this,
                                       version,
                                       srcn,
                                       spcDirAddr,
                                       addrADSR1Entry,
                                       addrADSR2Entry,
                                       addrTuningEntryHigh,
                                       addrTuningEntryLow);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  setGuessedLength();
  return true;
}

// ************
// PrismSnesRgn
// ************

PrismSnesRgn::PrismSnesRgn(PrismSnesInstr *instr,
                           PrismSnesVersion ver,
                           uint8_t srcn,
                           uint32_t spcDirAddr,
                           uint16_t addrADSR1Entry,
                           uint16_t addrADSR2Entry,
                           uint16_t addrTuningEntryHigh,
                           uint16_t addrTuningEntryLow) :
    VGMRgn(instr, addrADSR1Entry, 0),
    version(ver) {
  int16_t tuning = readByte(addrTuningEntryLow) | (readByte(addrTuningEntryHigh) << 8);

  double fine_tuning;
  double coarse_tuning;
  fine_tuning = modf(tuning / 256.0, &coarse_tuning) * 100.0;

  addChild(addrADSR1Entry, 1, "ADSR1");
  addChild(addrADSR2Entry, 1, "ADSR2");
  addUnityKey(93 - (int) coarse_tuning, addrTuningEntryHigh, 1);
  addFineTune((int16_t) fine_tuning, addrTuningEntryLow, 1);

  uint8_t adsr1 = readByte(addrADSR1Entry);
  uint8_t adsr2 = readByte(addrADSR2Entry);
  uint8_t gain = 0x9c;
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);

  // put a random release time, it would be better than plain key off (actual music engine never do key off)
  release_time = linearAmpDecayTimeToLinDBDecayTime(0.002 * SDSP_COUNTER_RATES[0x14], 0x7ff);

  setGuessedLength();
}

PrismSnesRgn::~PrismSnesRgn() {}

bool PrismSnesRgn::loadRgn() {
  return true;
}
