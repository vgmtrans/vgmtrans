/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "MoriSnesInstr.h"
#include "SNESDSP.h"
#include <spdlog/fmt/fmt.h>

// ****************
// MoriSnesInstrSet
// ****************

MoriSnesInstrSet::MoriSnesInstrSet(RawFile *file,
                                   MoriSnesVersion ver,
                                   uint32_t spcDirAddr,
                                   std::vector<uint16_t> instrumentAddresses,
                                   std::map<uint16_t, MoriSnesInstrHintDir> instrumentHints,
                                   const std::string &name) :
    VGMInstrSet(MoriSnesFormat::name, file, 0, 0, name), version(ver),
    spcDirAddr(spcDirAddr),
    instrumentAddresses(instrumentAddresses),
    instrumentHints(instrumentHints) {
}

MoriSnesInstrSet::~MoriSnesInstrSet() {}

bool MoriSnesInstrSet::parseHeader() {
  return true;
}

bool MoriSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();

  if (instrumentAddresses.size() == 0) {
    return false;
  }

  // calculate whole instrument collection size
  uint16_t instrSetStartAddress = 0xffff;
  uint16_t instrSetEndAddress = 0;
  for (uint8_t instrNum = 0; instrNum < instrumentAddresses.size(); instrNum++) {
    uint16_t instrAddress = instrumentAddresses[instrNum];

    uint16_t instrStartAddress = instrAddress;
    uint16_t instrEndAddress = instrAddress;
    if (!instrumentHints[instrAddress].percussion) {
      MoriSnesInstrHint *instrHint = &instrumentHints[instrAddress].instrHint;
      if (instrHint->startAddress < instrStartAddress) {
        instrStartAddress = instrHint->startAddress;
      }
      if (instrHint->startAddress + instrHint->size > instrEndAddress) {
        instrEndAddress = instrHint->startAddress + instrHint->size;
      }
    }
    else {
      for (uint8_t percNoteKey = 0; percNoteKey < instrumentHints[instrAddress].percHints.size(); percNoteKey++) {
        MoriSnesInstrHint *instrHint = &instrumentHints[instrAddress].percHints[percNoteKey];
        if (instrHint->startAddress < instrStartAddress) {
          instrStartAddress = instrHint->startAddress;
        }
        if (instrHint->startAddress + instrHint->size > instrEndAddress) {
          instrEndAddress = instrHint->startAddress + instrHint->size;
        }
      }
    }
    instrumentHints[instrAddress].startAddress = instrStartAddress;
    instrumentHints[instrAddress].size = instrEndAddress - instrStartAddress;

    if (instrStartAddress < instrSetStartAddress) {
      instrSetStartAddress = instrStartAddress;
    }
    if (instrEndAddress > instrSetEndAddress) {
      instrSetEndAddress = instrEndAddress;
    }
  }
  dwOffset = instrSetStartAddress;
  unLength = instrSetEndAddress - instrSetStartAddress;

  // load each instruments
  for (uint8_t instrNum = 0; instrNum < instrumentAddresses.size(); instrNum++) {
    uint16_t instrAddress = instrumentAddresses[instrNum];

    if (!instrumentHints[instrAddress].percussion) {
      MoriSnesInstrHint *instrHint = &instrumentHints[instrAddress].instrHint;

      uint16_t rgnAddress = instrHint->rgnAddress;
      if (rgnAddress == 0 || rgnAddress + 7 > 0x10000) {
        continue;
      }

      uint8_t srcn = readByte(rgnAddress);
      std::vector<uint8_t>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
      if (itrSRCN == usedSRCNs.end()) {
        usedSRCNs.push_back(srcn);
      }
    }
    else {
      for (uint8_t percNoteKey = 0; percNoteKey < instrumentHints[instrAddress].percHints.size(); percNoteKey++) {
        MoriSnesInstrHint *instrHint = &instrumentHints[instrAddress].percHints[percNoteKey];

        uint16_t rgnAddress = instrHint->rgnAddress;
        if (rgnAddress == 0 || rgnAddress + 7 > 0x10000) {
          continue;
        }

        uint8_t srcn = readByte(rgnAddress);
        std::vector<uint8_t>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
        if (itrSRCN == usedSRCNs.end()) {
          usedSRCNs.push_back(srcn);
        }
      }
    }

    MoriSnesInstr *newInstr = new MoriSnesInstr(
      this, version, instrNum, spcDirAddr, instrumentHints[instrAddress],
      fmt::format("Instrument {}", instrNum));
    aInstrs.push_back(newInstr);
  }

  if (aInstrs.size() == 0) {
    return false;
  }

  std::sort(usedSRCNs.begin(), usedSRCNs.end());
  SNESSampColl *newSampColl = new SNESSampColl(MoriSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// *************
// MoriSnesInstr
// *************

MoriSnesInstr::MoriSnesInstr(VGMInstrSet *instrSet,
                             MoriSnesVersion ver,
                             uint8_t instrNum,
                             uint32_t spcDirAddr,
                             const MoriSnesInstrHintDir &instrHintDir,
                             const std::string &name) :
    VGMInstr(instrSet, instrHintDir.startAddress, instrHintDir.size, 0, instrNum, name), version(ver),
    spcDirAddr(spcDirAddr),
    instrHintDir(instrHintDir) {
}

MoriSnesInstr::~MoriSnesInstr() {
}

bool MoriSnesInstr::loadInstr() {
  addChild(dwOffset, 1, "Melody/Percussion");

  if (!instrHintDir.percussion) {
    MoriSnesInstrHint *instrHint = &instrHintDir.instrHint;
    uint8_t srcn = readByte(instrHint->rgnAddress);

    uint32_t offDirEnt = spcDirAddr + (srcn * 4);
    if (offDirEnt + 4 > 0x10000) {
      return false;
    }

    addChild(instrHint->seqAddress, instrHint->seqSize, "Envelope Sequence");

    uint16_t addrSampStart = readShort(offDirEnt);
    MoriSnesRgn *rgn = new MoriSnesRgn(this, version, spcDirAddr, *instrHint);
    rgn->sampOffset = addrSampStart - spcDirAddr;
    addRgn(rgn);
  }
  else {
    for (uint8_t percNoteKey = 0; percNoteKey < instrHintDir.percHints.size(); percNoteKey++) {
      MoriSnesInstrHint *instrHint = &instrHintDir.percHints[percNoteKey];
      uint8_t srcn = readByte(instrHint->rgnAddress);

      uint32_t offDirEnt = spcDirAddr + (srcn * 4);
      if (offDirEnt + 4 > 0x10000) {
        return false;
      }

      auto seqOffsetName = fmt::format("Sequence Offset {}", percNoteKey);
      addChild(dwOffset + 1 + (percNoteKey * 2), 2, seqOffsetName);

      auto seqName = fmt::format("Envelope Sequence {}", percNoteKey);
      addChild(instrHint->seqAddress, instrHint->seqSize, seqName);

      uint16_t addrSampStart = readShort(offDirEnt);
      MoriSnesRgn *rgn = new MoriSnesRgn(this, version, spcDirAddr, *instrHint, percNoteKey);
      rgn->sampOffset = addrSampStart - spcDirAddr;
      addRgn(rgn);
    }
  }

  return true;
}

// ***********
// MoriSnesRgn
// ***********

MoriSnesRgn::MoriSnesRgn(MoriSnesInstr *instr,
                         MoriSnesVersion ver,
                         uint32_t /*spcDirAddr*/,
                         const MoriSnesInstrHint &instrHint,
                         int8_t percNoteKey) :
    VGMRgn(instr, instrHint.rgnAddress, 7),
    version(ver) {
  uint16_t rgnAddress = instrHint.rgnAddress;
  uint16_t curOffset = rgnAddress;

  uint8_t srcn = readByte(curOffset++);
  uint8_t adsr1 = readByte(curOffset++);
  uint8_t adsr2 = readByte(curOffset++);
  uint8_t gain = readByte(curOffset++);
  uint8_t keyOffDelay = readByte(curOffset++);
  int8_t key = readByte(curOffset++);
  uint8_t tuning = readByte(curOffset++);

  double fine_tuning;
  double coarse_tuning;
  const double pitch_fixer = log(4286.0 / 4096.0) / log(2) * 12;  // from pitch table ($10be vs $1000)
  fine_tuning = modf((key + (tuning / 256.0)) + pitch_fixer, &coarse_tuning);

  // normalize
  if (fine_tuning >= 0.5) {
    coarse_tuning += 1.0;
    fine_tuning -= 1.0;
  }
  else if (fine_tuning <= -0.5) {
    coarse_tuning -= 1.0;
    fine_tuning += 1.0;
  }

  addSampNum(srcn, rgnAddress, 1);
  addChild(rgnAddress + 1, 1, "ADSR1");
  addChild(rgnAddress + 2, 1, "ADSR2");
  addChild(rgnAddress + 3, 1, "GAIN");
  addChild(rgnAddress + 4, 1, "Key-Off Delay");
  addUnityKey(72 - (int) (coarse_tuning), rgnAddress + 5, 1);
  addFineTune((int16_t) (fine_tuning * 100.0), rgnAddress + 6, 1);
  if (instrHint.pan > 0) {
    pan = instrHint.pan / 32.0;
  }
  if (percNoteKey >= 0) {
    int8_t unityKeyCandidate = percNoteKey - (instrHint.transpose - unityKey); // correct?
    int8_t negativeUnityKeyAmount = 0;
    if (unityKeyCandidate < 0) {
      negativeUnityKeyAmount = -unityKeyCandidate;
      unityKeyCandidate = 0;
    }

    unityKey = unityKeyCandidate;
    fineTune -= negativeUnityKeyAmount * 100;
    keyLow = percNoteKey;
    keyHigh = percNoteKey;
  }
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

MoriSnesRgn::~MoriSnesRgn() {}

bool MoriSnesRgn::loadRgn() {
  return true;
}
