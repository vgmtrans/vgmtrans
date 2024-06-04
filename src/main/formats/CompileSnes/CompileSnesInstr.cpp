/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <spdlog/fmt/fmt.h>
#include "CompileSnesInstr.h"
#include "SNESDSP.h"

// *******************
// CompileSnesInstrSet
// *******************

CompileSnesInstrSet::CompileSnesInstrSet(RawFile *file,
                                         CompileSnesVersion ver,
                                         uint16_t addrTuningTable,
                                         uint16_t addrPitchTablePtrs,
                                         uint32_t spcDirAddr,
                                         const std::string &name)
    : VGMInstrSet(CompileSnesFormat::name, file, addrTuningTable, 0, name), version(ver),
      addrTuningTable(addrTuningTable),
      addrPitchTablePtrs(addrPitchTablePtrs),
      spcDirAddr(spcDirAddr) {
}

CompileSnesInstrSet::~CompileSnesInstrSet() {
}

bool CompileSnesInstrSet::GetHeaderInfo() {
  return true;
}

bool CompileSnesInstrSet::GetInstrPointers() {
  usedSRCNs.clear();
  for (uint8_t srcn = 0; srcn <= 0x3f; srcn++) {
    uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
    if (!SNESSampColl::IsValidSampleDir(rawFile(), addrDIRentry, true)) {
      continue;
    }

    uint16_t addrSampStart = GetShort(addrDIRentry);
    if (addrSampStart < spcDirAddr) {
      continue;
    }

    uint32_t ofsInstrEntry = addrTuningTable + (srcn * CompileSnesInstr::ExpectedSize(version));
    if (ofsInstrEntry + CompileSnesInstr::ExpectedSize(version) > 0x10000) {
      break;
    }

    if (version != COMPILESNES_ALESTE && version != COMPILESNES_JAKICRUSH) {
      uint8_t pitchTableIndex = GetByte(ofsInstrEntry + 1);
      if (pitchTableIndex >= 0x80) {
        // apparently it's too large
        continue;
      }
    }

    usedSRCNs.push_back(srcn);

    CompileSnesInstr *newInstr = new CompileSnesInstr(
      this, version, ofsInstrEntry, addrPitchTablePtrs, srcn, spcDirAddr,
      fmt::format("Instrument: {:#x}", srcn));
    aInstrs.push_back(newInstr);
  }
  if (aInstrs.size() == 0) {
    return false;
  }

  std::ranges::sort(usedSRCNs);
  SNESSampColl *newSampColl = new SNESSampColl(CompileSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->LoadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// ****************
// CompileSnesInstr
// ****************

CompileSnesInstr::CompileSnesInstr(VGMInstrSet *instrSet,
                                   CompileSnesVersion ver,
                                   uint16_t addrTuningTableItem,
                                   uint16_t addrPitchTablePtrs,
                                   uint8_t srcn,
                                   uint32_t spcDirAddr,
                                   const std::string &name)
    : VGMInstr(instrSet, addrTuningTableItem, CompileSnesInstr::ExpectedSize(ver), 0, srcn, name), version(ver),
      addrPitchTablePtrs(addrPitchTablePtrs),
      spcDirAddr(spcDirAddr) {}

CompileSnesInstr::~CompileSnesInstr() {}

bool CompileSnesInstr::LoadInstr() {
  uint32_t offDirEnt = spcDirAddr + (instrNum * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  uint16_t addrSampStart = GetShort(offDirEnt);

  CompileSnesRgn *rgn = new CompileSnesRgn(this, version, dwOffset, addrPitchTablePtrs);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  AddRgn(rgn);

  return true;
}

uint32_t CompileSnesInstr::ExpectedSize(CompileSnesVersion version) {
  if (version == COMPILESNES_ALESTE || version == COMPILESNES_JAKICRUSH) {
    return 1;
  } else {
    return 2;
  }
}

// **************
// CompileSnesRgn
// **************

CompileSnesRgn::CompileSnesRgn(CompileSnesInstr *instr,
                               CompileSnesVersion ver,
                               uint16_t addrTuningTableItem,
                               uint16_t addrPitchTablePtrs)
    : VGMRgn(instr, addrTuningTableItem, CompileSnesInstr::ExpectedSize(ver)),
      version(ver) {

  int8_t transpose = GetByte(addrTuningTableItem);
  AddUnityKey(transpose, addrTuningTableItem, 1);

  const uint16_t REGULAR_PITCH_TABLE[0x77] = {
      0x0012, 0x0013, 0x0014, 0x0015, 0x0017, 0x0018, 0x0019, 0x001b,
      0x001d, 0x001e, 0x0020, 0x0022, 0x0024, 0x0026, 0x0028, 0x002a,
      0x002e, 0x0030, 0x0032, 0x0036, 0x003a, 0x003c, 0x0040, 0x0044,
      0x0048, 0x004c, 0x0050, 0x0054, 0x005c, 0x0060, 0x0064, 0x006c,
      0x0074, 0x0078, 0x0080, 0x0088, 0x0090, 0x0098, 0x00a0, 0x00a8,
      0x00b8, 0x00c0, 0x00c8, 0x00d8, 0x00e8, 0x00f0, 0x0100, 0x0110,
      0x0120, 0x0130, 0x0140, 0x0150, 0x0170, 0x0180, 0x0190, 0x01b0,
      0x01d0, 0x01e0, 0x0200, 0x0220, 0x0240, 0x0260, 0x0280, 0x02b0,
      0x02d0, 0x0300, 0x0330, 0x0360, 0x0390, 0x03c0, 0x0400, 0x0440,
      0x0480, 0x04c0, 0x0510, 0x0550, 0x05b0, 0x0600, 0x0660, 0x06c0,
      0x0720, 0x0790, 0x0800, 0x0880, 0x0900, 0x0980, 0x0a10, 0x0ab0,
      0x0b50, 0x0c00, 0x0cb0, 0x0d70, 0x0e40, 0x0f20, 0x1000, 0x10f0,
      0x11f0, 0x1300, 0x1430, 0x1560, 0x16a0, 0x1800, 0x1960, 0x1af0,
      0x1c80, 0x1e30, 0x2000, 0x21e0, 0x23f0, 0x2610, 0x2850, 0x2ab0,
      0x2d40, 0x2ff0, 0x32d0, 0x35d0, 0x3900, 0x3c70, 0x3fff,
  };

  // load pitch table for the instrument
  std::vector<uint16_t> pitchTable;
  if (version == COMPILESNES_ALESTE || version == COMPILESNES_JAKICRUSH) {
    pitchTable.assign(std::begin(REGULAR_PITCH_TABLE), std::end(REGULAR_PITCH_TABLE));
  }
  else {
    uint8_t pitchTableIndex = GetByte(addrTuningTableItem + 1);
    addSimpleChild(dwOffset + 1, 1, "Pitch Table Index");

    if (pitchTableIndex == 0) {
      pitchTable.assign(std::begin(REGULAR_PITCH_TABLE), std::end(REGULAR_PITCH_TABLE));
    }
    else {
      uint32_t addrPitchTablePtr = addrPitchTablePtrs + (pitchTableIndex * 2);
      if (addrPitchTablePtr + 2 <= 0x10000) {
        uint32_t addrPitchTable = GetShort(addrPitchTablePtr) + 2;
        if (addrPitchTable + sizeof(REGULAR_PITCH_TABLE) <= 0x10000) {
          for (uint8_t key = 0; key < std::size(REGULAR_PITCH_TABLE); key++) {
            pitchTable.push_back(GetShort(addrPitchTable + (key * 2)));
          }
        }
      }

      if (pitchTable.empty()) {
        // unable to load specified pitch table
        pitchTable.assign(std::begin(REGULAR_PITCH_TABLE), std::end(REGULAR_PITCH_TABLE));
      }
    }
  }

  // search unity key (nearest to pitch $1000)
  uint8_t theUnityKey = 0;
  uint16_t bestPitchDistance = 0xffff;
  for (uint8_t key = 0; key < pitchTable.size(); key++) {
    uint16_t pitchDistance = abs(static_cast<int>(pitchTable[key]) - 0x1000);
    if (pitchDistance < bestPitchDistance) {
      bestPitchDistance = pitchDistance;
      theUnityKey = key;
    }
  }

  // correct the pitch difference
  const double pitch_fixer = pitchTable[theUnityKey] / 4096.0;
  double coarse_tuning;
  double fine_tuning = modf((log(pitch_fixer) / log(2.0)) * 12.0, &coarse_tuning);

  // normalize
  if (fine_tuning >= 0.5) {
    coarse_tuning += 1.0;
    fine_tuning -= 1.0;
  }
  else if (fine_tuning <= -0.5) {
    coarse_tuning -= 1.0;
    fine_tuning += 1.0;
  }

  // apply per-instrument transpose
  coarse_tuning += transpose;

  // set final result
  unityKey = theUnityKey - 24 - static_cast<int>(coarse_tuning);
  fineTune = static_cast<int16_t>(fine_tuning * 100.0);

  uint8_t adsr1 = 0x8f;
  uint8_t adsr2 = 0xe0;
  uint8_t gain = 0;
  SNESConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

CompileSnesRgn::~CompileSnesRgn() {}

bool CompileSnesRgn::LoadRgn() {
  return true;
}
