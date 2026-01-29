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

bool TamSoftPS1InstrSet::parseHeader() {
  if (offset() + 0x800 > vgmFile()->endOffset()) {
    return false;
  }

  uint32_t sampCollSize = readWord(0x3fc);
  if (offset() + 0x800 + sampCollSize > vgmFile()->endOffset()) {
    return false;
  }
  setLength(0x800 + sampCollSize);

  return true;
}

bool TamSoftPS1InstrSet::parseInstrPointers() {
  std::vector<SizeOffsetPair> vagLocations;

  for (uint32_t instrNum = 0; instrNum < 256; instrNum++) {
    bool vagLoop;
    uint32_t vagOffset = 0x800 + readWord(offset() + 4 * instrNum);
    if (vagOffset < length()) {
      SizeOffsetPair vagLocation(vagOffset - 0x800, PSXSamp::getSampleLength(rawFile(), vagOffset, offset() + length(), vagLoop));
      vagLocations.push_back(vagLocation);

      TamSoftPS1Instr *newInstr = new TamSoftPS1Instr(this, instrNum,
        fmt::format("Instrument {}", instrNum));
      aInstrs.push_back(newInstr);
    }
  }

  if (aInstrs.size() == 0) {
    return false;
  }

  sampColl = new PSXSampColl(TamSoftPS1Format::name, this, offset() + 0x800, length() - 0x800, vagLocations);
  return true;
}

// ***************
// TamSoftPS1Instr
// ***************

TamSoftPS1Instr::TamSoftPS1Instr(TamSoftPS1InstrSet *instrSet, uint8_t instrNum, const std::string &name) :
    VGMInstr(instrSet, instrSet->offset() + 4 * instrNum, 0x400 + 4, 0, instrNum, name) {
}

TamSoftPS1Instr::~TamSoftPS1Instr() {
}

bool TamSoftPS1Instr::loadInstr() {
  TamSoftPS1InstrSet *parInstrSet = (TamSoftPS1InstrSet *) this->parInstrSet;

  addChild(offset(), 4, "Sample Offset");

  TamSoftPS1Rgn *rgn = new TamSoftPS1Rgn(this, offset() + 0x400, parInstrSet->ps2);
  rgn->sampNum = instrNum;
  addRgn(rgn);
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

  uint16_t adsr1 = readShort(offset);
  uint16_t adsr2 = readShort(offset + 2);
  if (ps2) {
    // Choro Q HG2 default ADSR (set by progInitWork)
    adsr1 = 0x13FF;
    adsr2 = 0x5FC5;
  }
  psxConvADSR<TamSoftPS1Rgn>(this, adsr1, adsr2, ps2);
}

TamSoftPS1Rgn::~TamSoftPS1Rgn() {
}

bool TamSoftPS1Rgn::loadRgn() {
  return true;
}
