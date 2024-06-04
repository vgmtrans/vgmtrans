#include "formats/PS1/Vab.h"
#include "Format.h"			//include PS1-specific format header file when it is ready
#include "PSXSPU.h"
#include "formats/PS1/PS1Format.h"

using namespace std;

Vab::Vab(RawFile *file, uint32_t offset)
    : VGMInstrSet(PS1Format::name, file, offset, 0, "VAB") {
}

Vab::~Vab() {
}


bool Vab::GetHeaderInfo() {
  uint32_t nEndOffset = GetEndOffset();
  uint32_t nMaxLength = nEndOffset - dwOffset;

  if (nMaxLength < 0x20) {
    return false;
  }

  VGMHeader *vabHdr = addHeader(dwOffset, 0x20, "VAB Header");
  vabHdr->addSimpleChild(dwOffset + 0x00, 4, "ID");
  vabHdr->addSimpleChild(dwOffset + 0x04, 4, "Version");
  vabHdr->addSimpleChild(dwOffset + 0x08, 4, "VAB ID");
  vabHdr->addSimpleChild(dwOffset + 0x0c, 4, "Total Size");
  vabHdr->addSimpleChild(dwOffset + 0x10, 2, "Reserved");
  vabHdr->addSimpleChild(dwOffset + 0x12, 2, "Number of Programs");
  vabHdr->addSimpleChild(dwOffset + 0x14, 2, "Number of Tones");
  vabHdr->addSimpleChild(dwOffset + 0x16, 2, "Number of VAGs");
  vabHdr->addSimpleChild(dwOffset + 0x18, 1, "Master Volume");
  vabHdr->addSimpleChild(dwOffset + 0x19, 1, "Master Pan");
  vabHdr->addSimpleChild(dwOffset + 0x1a, 1, "Bank Attributes 1");
  vabHdr->addSimpleChild(dwOffset + 0x1b, 1, "Bank Attributes 2");
  vabHdr->addSimpleChild(dwOffset + 0x1c, 4, "Reserved");

  GetBytes(dwOffset, 0x20, &hdr);

  return true;
}

bool Vab::GetInstrPointers() {
  uint32_t nEndOffset = GetEndOffset();

  uint32_t offProgs = dwOffset + 0x20;
  uint32_t offToneAttrs = offProgs + (16 * 128);

  uint16_t numPrograms = GetShort(dwOffset + 0x12);
  uint16_t numTones = GetShort(dwOffset + 0x14);
  uint16_t numVAGs = GetShort(dwOffset + 0x16);

  uint32_t offVAGOffsets = offToneAttrs + (32 * 16 * numPrograms);

  VGMHeader *progsHdr = addHeader(offProgs, 16 * 128, "Program Table");
  VGMHeader *toneAttrsHdr = addHeader(offToneAttrs, 32 * 16, "Tone Attributes Table");

  if (numPrograms > 128) {
    L_ERROR("Too many programs {}  Offset: 0x{:X}", numPrograms,  dwOffset);
    return false;
  }
  if (numVAGs > 255) {
    L_ERROR("Too many VAGs {}  Offset: 0x{:X}", numVAGs,  dwOffset);
    return false;
  }

  // Load each instruments.
  //
  // Rule 1. Valid instrument pointers are not always sequentially located from 0 to (numProgs - 1).
  // Number of tones can be 0. That's an empty instrument. We need to ignore it.
  // See Clock Tower PSF for example.
  //
  // Rule 2. Do not load programs more than number of programs. Even if a program table value is provided.
  // Otherwise an out-of-order access can be caused in Tone Attributes Table.
  // See the swimming event BGM of Aitakute... ~your smiles in my heart~ for example. (github issue #115)
  uint32_t numProgramsLoaded = 0;
  for (uint32_t progIndex = 0; progIndex < 128 && numProgramsLoaded < numPrograms; progIndex++) {
    uint32_t offCurrProg = offProgs + (progIndex * 16);
    uint32_t offCurrToneAttrs = offToneAttrs + (uint32_t) (aInstrs.size() * 32 * 16);

    if (offCurrToneAttrs + (32 * 16) > nEndOffset) {
      break;
    }

    uint8_t numTonesPerInstr = GetByte(offCurrProg);
    if (numTonesPerInstr > 32) {
      L_WARN("Too many tones {} in Program #{}.", numTonesPerInstr, progIndex);
    }
    else if (numTonesPerInstr != 0) {
      VabInstr *newInstr = new VabInstr(this, offCurrToneAttrs, 0x20 * 16, 0, progIndex);
      aInstrs.push_back(newInstr);
      GetBytes(offCurrProg, 0x10, &newInstr->attr);

      VGMHeader *hdr = progsHdr->addHeader(offCurrProg, 0x10, "Program");
      hdr->addSimpleChild(offCurrProg + 0x00, 1, "Number of Tones");
      hdr->addSimpleChild(offCurrProg + 0x01, 1, "Volume");
      hdr->addSimpleChild(offCurrProg + 0x02, 1, "Priority");
      hdr->addSimpleChild(offCurrProg + 0x03, 1, "Mode");
      hdr->addSimpleChild(offCurrProg + 0x04, 1, "Pan");
      hdr->addSimpleChild(offCurrProg + 0x05, 1, "Reserved");
      hdr->addSimpleChild(offCurrProg + 0x06, 2, "Attribute");
      hdr->addSimpleChild(offCurrProg + 0x08, 4, "Reserved");
      hdr->addSimpleChild(offCurrProg + 0x0c, 4, "Reserved");

      newInstr->masterVol = GetByte(offCurrProg + 0x01);

      toneAttrsHdr->unLength = offCurrToneAttrs + (32 * 16) - offToneAttrs;

      numProgramsLoaded++;
    }
  }

  if ((offVAGOffsets + 2 * 256) <= nEndOffset) {
    char name[256];
    std::vector<SizeOffsetPair> vagLocations;
    uint32_t totalVAGSize = 0;
    VGMHeader *vagOffsetHdr = addHeader(offVAGOffsets, 2 * 256, "VAG Pointer Table");

    uint32_t vagStartOffset = offVAGOffsets + 2 * 256;
    uint32_t vagOffset = vagStartOffset;

    for (uint32_t i = 0; i < numVAGs; i++) {
      uint32_t vagSize = GetShort(offVAGOffsets + i * 2) * 8;

      snprintf(name, 256, "VAG Size /8 #%u", i);
      vagOffsetHdr->addSimpleChild(offVAGOffsets + i * 2, 2, name);

      if (vagOffset + vagSize <= nEndOffset) {
        vagLocations.emplace_back(vagOffset, vagSize);
        totalVAGSize += vagSize;
      }
      else {
        L_WARN("VAG #{} pointer (offset=0x{:08X}, size={}) is invalid.", i, vagOffset, vagSize);
      }

      vagOffset += vagSize;
    }
    unLength = vagStartOffset - dwOffset;

    // single VAB file?
    if (dwOffset == 0 && vagLocations.size() != 0) {
      // load samples as well
      PSXSampColl *newSampColl = new PSXSampColl(formatName(), this, vagStartOffset, totalVAGSize, vagLocations);
      if (newSampColl->LoadVGMFile()) {
        pRoot->AddVGMFile(newSampColl);
        //this->sampColl = newSampColl;
      }
      else {
        delete newSampColl;
      }
    }
  }

  return true;
}






// ********
// VabInstr
// ********

VabInstr::VabInstr(VGMInstrSet *instrSet,
                   uint32_t offset,
                   uint32_t length,
                   uint32_t theBank,
                   uint32_t theInstrNum,
                   const string &name)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, name),
      masterVol(127) {
}

VabInstr::~VabInstr(void) {
}


bool VabInstr::LoadInstr() {
  int8_t numRgns = attr.tones;
  for (int i = 0; i < numRgns; i++) {
    VabRgn *rgn = new VabRgn(this, dwOffset + i * 0x20);
    if (!rgn->LoadRgn()) {
      delete rgn;
      return false;
    }
    AddRgn(rgn);
  }
  return true;
}





// ******
// VabRgn
// ******

VabRgn::VabRgn(VabInstr *instr, uint32_t offset)
    : VGMRgn(instr, offset) {
}


bool VabRgn::LoadRgn() {
  VabInstr *instr = (VabInstr *) parInstr;
  unLength = 0x20;
  GetBytes(dwOffset, 0x20, &attr);

  AddGeneralItem(dwOffset, 1, "Priority");
  AddGeneralItem(dwOffset + 1, 1, "Mode (use reverb?)");
  AddVolume((GetByte(dwOffset + 2) * instr->masterVol) / (127.0 * 127.0), dwOffset + 2, 1);
  AddPan(GetByte(dwOffset + 3), dwOffset + 3);
  AddUnityKey(GetByte(dwOffset + 4), dwOffset + 4);
  AddGeneralItem(dwOffset + 5, 1, "Pitch Tune");
  AddKeyLow(GetByte(dwOffset + 6), dwOffset + 6);
  AddKeyHigh(GetByte(dwOffset + 7), dwOffset + 7);
  AddGeneralItem(dwOffset + 8, 1, "Vibrato Width");
  AddGeneralItem(dwOffset + 9, 1, "Vibrato Time");
  AddGeneralItem(dwOffset + 10, 1, "Portamento Width");
  AddGeneralItem(dwOffset + 11, 1, "Portamento Holding Time");
  AddGeneralItem(dwOffset + 12, 1, "Pitch Bend Min");
  AddGeneralItem(dwOffset + 13, 1, "Pitch Bend Max");
  AddGeneralItem(dwOffset + 14, 1, "Reserved");
  AddGeneralItem(dwOffset + 15, 1, "Reserved");
  AddGeneralItem(dwOffset + 16, 2, "ADSR1");
  AddGeneralItem(dwOffset + 18, 2, "ADSR2");
  AddGeneralItem(dwOffset + 20, 2, "Parent Program");
  AddSampNum(GetShort(dwOffset + 22) - 1, dwOffset + 22, 2);
  AddGeneralItem(dwOffset + 24, 2, "Reserved");
  AddGeneralItem(dwOffset + 26, 2, "Reserved");
  AddGeneralItem(dwOffset + 28, 2, "Reserved");
  AddGeneralItem(dwOffset + 30, 2, "Reserved");
  ADSR1 = attr.adsr1;
  ADSR2 = attr.adsr2;
  if ((int) sampNum < 0)
    sampNum = 0;

  if (keyLow > keyHigh) {
    L_ERROR("Low Key ({}) is higher than High Key ({})  Offset: 0x{:X}", keyLow, keyHigh, dwOffset);
    return false;
  }


  // gocha: AFAIK, the valid range of pitch is 0-127. It must not be negative.
  // If it exceeds 127, driver clips the value and it will become 127. (In Hokuto no Ken, at least)
  // I am not sure if the interpretation of this value depends on a driver or VAB version.
  // The following code takes the byte as signed, since it could be a typical extended implementation.
  int8_t ft = (int8_t) GetByte(dwOffset + 5);
  double cents = ft * 100.0 / 128.0;
  SetFineTune((int16_t) cents);

  PSXConvADSR<VabRgn>(this, ADSR1, ADSR2, false);
  return true;
}
