#include "TamSoftPS1Instr.h"
#include "PSXSPU.h"

// ******************
// TamSoftPS1InstrSet
// ******************

TamSoftPS1InstrSet::TamSoftPS1InstrSet(RawFile *file, uint32_t offset, bool ps2, const std::string &name) :
    VGMInstrSet(TamSoftPS1Format::name, file, offset, 0, name), ps2(ps2) {
}

TamSoftPS1InstrSet::~TamSoftPS1InstrSet() {
}

bool TamSoftPS1InstrSet::GetHeaderInfo() {
  if (dwOffset + 0x800 > vgmFile()->GetEndOffset()) {
    return false;
  }

  uint32_t sampCollSize = GetWord(0x3fc);
  if (dwOffset + 0x800 + sampCollSize > vgmFile()->GetEndOffset()) {
    return false;
  }
  unLength = 0x800 + sampCollSize;

  return true;
}

bool TamSoftPS1InstrSet::GetInstrPointers() {
  std::vector<SizeOffsetPair> vagLocations;

  for (uint32_t instrNum = 0; instrNum < 256; instrNum++) {
    bool vagLoop;
    uint32_t vagOffset = 0x800 + GetWord(dwOffset + 4 * instrNum);
    if (vagOffset < unLength) {
      SizeOffsetPair vagLocation(vagOffset - 0x800, PSXSamp::GetSampleLength(rawFile(), vagOffset, dwOffset + unLength, vagLoop));
      vagLocations.push_back(vagLocation);

      TamSoftPS1Instr *newInstr = new TamSoftPS1Instr(this, instrNum,
        fmt::format("Instrument {}", instrNum));
      aInstrs.push_back(newInstr);
    }
  }

  if (aInstrs.size() == 0) {
    return false;
  }

  PSXSampColl *newSampColl = new PSXSampColl(TamSoftPS1Format::name, this, dwOffset + 0x800, unLength - 0x800, vagLocations);
  if (!newSampColl->LoadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// ***************
// TamSoftPS1Instr
// ***************

TamSoftPS1Instr::TamSoftPS1Instr(TamSoftPS1InstrSet *instrSet, uint8_t instrNum, const std::string &name) :
    VGMInstr(instrSet, instrSet->dwOffset + 4 * instrNum, 0x400 + 4, 0, instrNum, name) {
}

TamSoftPS1Instr::~TamSoftPS1Instr() {
}

bool TamSoftPS1Instr::LoadInstr() {
  TamSoftPS1InstrSet *parInstrSet = (TamSoftPS1InstrSet *) this->parInstrSet;

  addChild(dwOffset, 4, "Sample Offset");

  TamSoftPS1Rgn *rgn = new TamSoftPS1Rgn(this, dwOffset + 0x400, parInstrSet->ps2);
  rgn->sampNum = instrNum;
  AddRgn(rgn);
  return true;
}

// *************
// TamSoftPS1Rgn
// *************

TamSoftPS1Rgn::TamSoftPS1Rgn(TamSoftPS1Instr *instr, uint32_t offset, bool ps2) :
    VGMRgn(instr, offset, 4) {
  unityKey = TAMSOFTPS1_KEY_OFFSET + 48;
  addChild(offset, 2, "ADSR1");
  addChild(offset + 2, 2, "ADSR2");

  uint16_t adsr1 = GetShort(offset);
  uint16_t adsr2 = GetShort(offset + 2);
  if (ps2) {
    // Choro Q HG2 default ADSR (set by progInitWork)
    adsr1 = 0x13FF;
    adsr2 = 0x5FC5;
  }
  PSXConvADSR<TamSoftPS1Rgn>(this, adsr1, adsr2, ps2);
}

TamSoftPS1Rgn::~TamSoftPS1Rgn() {
}

bool TamSoftPS1Rgn::LoadRgn() {
  return true;
}
