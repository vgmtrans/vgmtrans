/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PrismSnesInstr.h"

#include "base/Types.h"
#include "SNESDSP.h"

#include <spdlog/fmt/fmt.h>

// *****************
// PrismSnesInstrSet
// *****************

PrismSnesInstrSet::PrismSnesInstrSet(RawFile *file,
                                     PrismSnesVersion ver,
                                     u32 spcDirAddress,
                                     u16 adsr1TableAddress,
                                     u16 adsr2TableAddress,
                                     u16 tuningTableHighAddress,
                                     u16 tuningTableLowAddress,
                                     const std::string &name) :
    VGMInstrSet(PrismSnesFormat::name, file, adsr1TableAddress, 0, name), version(ver),
    spcDirAddr(spcDirAddress),
    addrADSR1Table(adsr1TableAddress),
    addrADSR2Table(adsr2TableAddress),
    addrTuningTableHigh(tuningTableHighAddress),
    addrTuningTableLow(tuningTableLowAddress) {}

PrismSnesInstrSet::~PrismSnesInstrSet() {}

bool PrismSnesInstrSet::parseHeader() {
  return true;
}

bool PrismSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();
  for (u16 srcn16 = 0; srcn16 < 0x100; srcn16++) {
    u8 srcn = (u8)srcn16;

    u32 addrDIRentry = spcDirAddr + (srcn * 4);
    if (!SNESSampColl::isValidSampleDir(rawFile(), addrDIRentry, true)) {
      continue;
    }

    u16 addrSampStart = readShort(addrDIRentry);
    if (addrSampStart < spcDirAddr) {
      continue;
    }

    u32 ofsADSR1Entry;
    ofsADSR1Entry = addrADSR1Table + srcn;
    if (ofsADSR1Entry + 1 > 0x10000) {
      break;
    }

    u32 ofsADSR2Entry;
    ofsADSR2Entry = addrADSR2Table + srcn;
    if (ofsADSR2Entry + 1 > 0x10000) {
      break;
    }

    u32 ofsTuningEntryHigh;
    ofsTuningEntryHigh = addrTuningTableHigh + srcn;
    if (ofsTuningEntryHigh + 1 > 0x10000) {
      break;
    }

    u32 ofsTuningEntryLow;
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
                               u8 sampleNumber,
                               u32 spcDirAddress,
                               u16 adsr1EntryAddress,
                               u16 adsr2EntryAddress,
                               u16 tuningEntryHighAddress,
                               u16 tuningEntryLowAddress,
                               const std::string &name) :
    VGMInstr(instrSet, adsr1EntryAddress, 0, sampleNumber >> 7, sampleNumber & 0x7f, name), version(ver),
    srcn(sampleNumber),
    spcDirAddr(spcDirAddress),
    addrADSR1Entry(adsr1EntryAddress),
    addrADSR2Entry(adsr2EntryAddress),
    addrTuningEntryHigh(tuningEntryHighAddress),
    addrTuningEntryLow(tuningEntryLowAddress) {
}

PrismSnesInstr::~PrismSnesInstr() {
}

bool PrismSnesInstr::loadInstr() {
  u32 offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  u16 addrSampStart = readShort(offDirEnt);

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
                           u8 srcn,
                           u32 spcDirAddr,
                           u16 addrADSR1Entry,
                           u16 addrADSR2Entry,
                           u16 addrTuningEntryHigh,
                           u16 addrTuningEntryLow) :
    VGMRgn(instr, addrADSR1Entry, 0),
    version(ver) {
  s16 tuning = readByte(addrTuningEntryLow) | (readByte(addrTuningEntryHigh) << 8);

  double fine_tuning;
  double coarse_tuning;
  fine_tuning = modf(tuning / 256.0, &coarse_tuning) * 100.0;

  addChild(addrADSR1Entry, 1, "ADSR1");
  addChild(addrADSR2Entry, 1, "ADSR2");
  addUnityKey(93 - (int) coarse_tuning, addrTuningEntryHigh, 1);
  addFineTune((s16) fine_tuning, addrTuningEntryLow, 1);

  u8 adsr1 = readByte(addrADSR1Entry);
  u8 adsr2 = readByte(addrADSR2Entry);
  u8 gain = 0x9c;
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);

  // put a random release time, it would be better than plain key off (actual music engine never do key off)
  release_time = linearAmpDecayTimeToLinDBDecayTime(0.002 * SDSP_COUNTER_RATES[0x14]);

  setGuessedLength();
}

PrismSnesRgn::~PrismSnesRgn() {}

bool PrismSnesRgn::loadRgn() {
  return true;
}
