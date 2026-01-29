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


bool Vab::parseHeader() {
  uint32_t nEndOffset = endOffset();
  uint32_t nMaxLength = nEndOffset - offset();

  if (nMaxLength < 0x20) {
    return false;
  }

  VGMHeader *vabHdr = addHeader(offset(), 0x20, "VAB Header");
  vabHdr->addChild(offset() + 0x00, 4, "ID");
  vabHdr->addChild(offset() + 0x04, 4, "Version");
  vabHdr->addChild(offset() + 0x08, 4, "VAB ID");
  vabHdr->addChild(offset() + 0x0c, 4, "Total Size");
  vabHdr->addChild(offset() + 0x10, 2, "Reserved");
  vabHdr->addChild(offset() + 0x12, 2, "Number of Programs");
  vabHdr->addChild(offset() + 0x14, 2, "Number of Tones");
  vabHdr->addChild(offset() + 0x16, 2, "Number of VAGs");
  vabHdr->addChild(offset() + 0x18, 1, "Master Volume");
  vabHdr->addChild(offset() + 0x19, 1, "Master Pan");
  vabHdr->addChild(offset() + 0x1a, 1, "Bank Attributes 1");
  vabHdr->addChild(offset() + 0x1b, 1, "Bank Attributes 2");
  vabHdr->addChild(offset() + 0x1c, 4, "Reserved");

  readBytes(offset(), 0x20, &hdr);

  return true;
}

bool Vab::parseInstrPointers() {
  uint32_t nEndOffset = endOffset();

  uint32_t offProgs = offset() + 0x20;
  uint32_t offToneAttrs = offProgs + (16 * 128);

  uint16_t numPrograms = readShort(offset() + 0x12);
  uint16_t numTones = readShort(offset() + 0x14);
  uint16_t numVAGs = readShort(offset() + 0x16);

  uint32_t offVAGOffsets = offToneAttrs + (32 * 16 * numPrograms);

  VGMHeader *progsHdr = addHeader(offProgs, 16 * 128, "Program Table");
  VGMHeader *toneAttrsHdr = addHeader(offToneAttrs, 32 * 16, "Tone Attributes Table");

  if (numPrograms > 128) {
    L_ERROR("Too many programs {}  Offset: 0x{:X}", numPrograms,  offset());
    return false;
  }
  if (numVAGs > 255) {
    L_ERROR("Too many VAGs {}  Offset: 0x{:X}", numVAGs,  offset());
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

    uint8_t numTonesPerInstr = readByte(offCurrProg);
    if (numTonesPerInstr > 32) {
      L_WARN("Too many tones {} in Program #{}.", numTonesPerInstr, progIndex);
    }
    else if (numTonesPerInstr != 0) {
      auto instrName = fmt::format("Instrument {:d}", progIndex);
      VabInstr *newInstr = new VabInstr(this, offCurrToneAttrs, 0x20 * 16, 0, progIndex, instrName);
      aInstrs.push_back(newInstr);
      readBytes(offCurrProg, 0x10, &newInstr->attr);

      const auto progName = fmt::format("Program {:d}", progIndex);
      VGMHeader *hdr = progsHdr->addHeader(offCurrProg, 0x10, progName);
      hdr->addChild(offCurrProg + 0x00, 1, "Number of Tones");
      hdr->addChild(offCurrProg + 0x01, 1, "Volume");
      hdr->addChild(offCurrProg + 0x02, 1, "Priority");
      hdr->addChild(offCurrProg + 0x03, 1, "Mode");
      hdr->addChild(offCurrProg + 0x04, 1, "Pan");
      hdr->addChild(offCurrProg + 0x05, 1, "Reserved");
      hdr->addChild(offCurrProg + 0x06, 2, "Attribute");
      hdr->addChild(offCurrProg + 0x08, 4, "Reserved");
      hdr->addChild(offCurrProg + 0x0c, 4, "Reserved");

      newInstr->masterVol = readByte(offCurrProg + 0x01);

      toneAttrsHdr->setLength(offCurrToneAttrs + (32 * 16) - offToneAttrs);

      numProgramsLoaded++;
    }
  }

  if ((offVAGOffsets + 2 * 256) <= nEndOffset) {
    char name[256];
    uint32_t totalVAGSize = 0;
    VGMHeader *vagOffsetHdr = addHeader(offVAGOffsets, 2 * 256, "VAG Pointer Table");

    uint32_t vagStartOffset = offVAGOffsets + 2 * 256;
    uint32_t vagOffset = 0;

    for (uint32_t i = 1; i < numVAGs + 1; i++) {
      uint32_t vagSize = readShort(offVAGOffsets + i * 2) * 8;

      snprintf(name, 256, "VAG Size /8 #%u", i);
      vagOffsetHdr->addChild(offVAGOffsets + i * 2, 2, name);

      auto absoluteVagOffset = vagStartOffset + vagOffset;
      if (absoluteVagOffset + vagSize <= nEndOffset) {
        m_vagLocations.emplace_back(vagOffset, vagSize);
        totalVAGSize += vagSize;
      }
      else {
        L_WARN("VAG #{} pointer (offset=0x{:08X}, size={}) is invalid.", i, absoluteVagOffset, vagSize);
      }

      vagOffset += vagSize;
    }
    setLength(vagStartOffset - offset());
  }

  return true;
}

bool Vab::isViableSampCollMatch(VGMSampColl* sampColl) {
  int sampleIndex = 0;
  auto sampCollOffset = sampColl->offset();
  for (auto& vagLoc : m_vagLocations) {
    if (vagLoc.offset == 0 && vagLoc.size == 0)
      continue;

    if (sampleIndex >= sampColl->samples.size())
      return false;

    auto sample = sampColl->samples[sampleIndex++];
    auto sampleRelOffset = sample->offset() - sampCollOffset;
    if (sampleRelOffset != vagLoc.offset ||
      (sample->length() > vagLoc.size + 32 || sample->length() < vagLoc.size))
      return false;
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

VabInstr::~VabInstr() {
}


bool VabInstr::loadInstr() {
  int8_t numRgns = attr.tones;
  for (int i = 0; i < numRgns; i++) {
    VabRgn *rgn = new VabRgn(this, offset() + i * 0x20);
    if (!rgn->loadRgn()) {
      delete rgn;
      continue;
    }
    addRgn(rgn);
  }
  return true;
}

// ******
// VabRgn
// ******

VabRgn::VabRgn(VabInstr *instr, uint32_t offset)
    : VGMRgn(instr, offset) {
}

bool VabRgn::loadRgn() {
  VabInstr *instr = (VabInstr *) parInstr;
  setLength(0x20);
  readBytes(offset(), 0x20, &attr);

  addGeneralItem(offset(), 1, "Priority");
  addGeneralItem(offset() + 1, 1, "Mode (use reverb?)");
  addVolume((readByte(offset() + 2) * instr->masterVol) / (127.0 * 127.0), offset() + 2, 1);
  addPan(readByte(offset() + 3), offset() + 3);
  addUnityKey(readByte(offset() + 4), offset() + 4);
  addGeneralItem(offset() + 5, 1, "Pitch Tune");
  addKeyLow(readByte(offset() + 6), offset() + 6);
  addKeyHigh(readByte(offset() + 7), offset() + 7);
  addGeneralItem(offset() + 8, 1, "Vibrato Width");
  addGeneralItem(offset() + 9, 1, "Vibrato Time");
  addGeneralItem(offset() + 10, 1, "Portamento Width");
  addGeneralItem(offset() + 11, 1, "Portamento Holding Time");
  addGeneralItem(offset() + 12, 1, "Pitch Bend Min");
  addGeneralItem(offset() + 13, 1, "Pitch Bend Max");
  addGeneralItem(offset() + 14, 1, "Reserved");
  addGeneralItem(offset() + 15, 1, "Reserved");
  addADSRValue(offset() + 16, 2, "ADSR1");
  addADSRValue(offset() + 18, 2, "ADSR2");
  addGeneralItem(offset() + 20, 2, "Parent Program");
  addSampNum(readShort(offset() + 22) - 1, offset() + 22, 2);
  addGeneralItem(offset() + 24, 2, "Reserved");
  addGeneralItem(offset() + 26, 2, "Reserved");
  addGeneralItem(offset() + 28, 2, "Reserved");
  addGeneralItem(offset() + 30, 2, "Reserved");
  ADSR1 = attr.adsr1;
  ADSR2 = attr.adsr2;
  if ((int) sampNum < 0)
    sampNum = 0;

  if (keyLow > keyHigh) {
    // This error may be present in actual game data. Example: Final Fantasy Origins: Chaos' Temple
    L_ERROR("Low Key ({}) is higher than High Key ({})  Offset: 0x{:X}", keyLow, keyHigh, offset());
    return false;
  }


  // gocha: AFAIK, the valid range of pitch is 0-127. It must not be negative.
  // If it exceeds 127, driver clips the value and it will become 127. (In Hokuto no Ken, at least)
  // I am not sure if the interpretation of this value depends on a driver or VAB version.
  uint8_t ft = readByte(offset() + 5);
  ft = std::min(ft, static_cast<u8>(127));
  double cents = ft * 100.0 / 128.0;
  setFineTune((int16_t) cents);

  psxConvADSR<VabRgn>(this, ADSR1, ADSR2, false);
  return true;
}
