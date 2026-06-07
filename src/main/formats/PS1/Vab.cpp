#include "formats/PS1/Vab.h"

#include "base/Types.h"
#include "Format.h"			//include PS1-specific format header file when it is ready
#include "formats/PS1/PS1Format.h"
#include "PSXSPU.h"

#include <memory>

using namespace std;

Vab::Vab(RawFile *file, u32 offset)
    : VGMInstrSet(PS1Format::name, file, offset, 0, "VAB") {
}

Vab::~Vab() {
}


bool Vab::parseHeader() {
  u32 nEndOffset = endOffset();
  u32 nMaxLength = nEndOffset - offset();

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
  u32 nEndOffset = endOffset();

  u32 offProgs = offset() + 0x20;
  u32 offToneAttrs = offProgs + (16 * 128);

  u16 numPrograms = readShort(offset() + 0x12);
  // Header +0x14 stores the total tone count; program entries below carry the per-program counts.
  u16 numVAGs = readShort(offset() + 0x16);

  u32 offVAGOffsets = offToneAttrs + (32 * 16 * numPrograms);

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
  u32 numProgramsLoaded = 0;
  for (u32 progIndex = 0; progIndex < 128 && numProgramsLoaded < numPrograms; progIndex++) {
    u32 offCurrProg = offProgs + (progIndex * 16);
    u32 offCurrToneAttrs = offToneAttrs + (u32) (instrCount() * 32 * 16);

    if (offCurrToneAttrs + (32 * 16) > nEndOffset) {
      break;
    }

    u8 numTonesPerInstr = readByte(offCurrProg);
    if (numTonesPerInstr > 32) {
      L_WARN("Too many tones {} in Program #{}.", numTonesPerInstr, progIndex);
    }
    else if (numTonesPerInstr != 0) {
      auto instrName = fmt::format("Instrument {:d}", progIndex);
      VabInstr *newInstr = addInstr<VabInstr>(this, offCurrToneAttrs, 0x20 * 16, 0, progIndex, instrName);
      readBytes(offCurrProg, 0x10, &newInstr->attr);

      const auto progName = fmt::format("Program {:d}", progIndex);
      VGMHeader *progHdr = progsHdr->addHeader(offCurrProg, 0x10, progName);
      progHdr->addChild(offCurrProg + 0x00, 1, "Number of Tones");
      progHdr->addChild(offCurrProg + 0x01, 1, "Volume");
      progHdr->addChild(offCurrProg + 0x02, 1, "Priority");
      progHdr->addChild(offCurrProg + 0x03, 1, "Mode");
      progHdr->addChild(offCurrProg + 0x04, 1, "Pan");
      progHdr->addChild(offCurrProg + 0x05, 1, "Reserved");
      progHdr->addChild(offCurrProg + 0x06, 2, "Attribute");
      progHdr->addChild(offCurrProg + 0x08, 4, "Reserved");
      progHdr->addChild(offCurrProg + 0x0c, 4, "Reserved");

      newInstr->masterVol = readByte(offCurrProg + 0x01);

      toneAttrsHdr->setLength(offCurrToneAttrs + (32 * 16) - offToneAttrs);

      numProgramsLoaded++;
    }
  }

  if ((offVAGOffsets + 2 * 256) <= nEndOffset) {
    char name[256];
    VGMHeader *vagOffsetHdr = addHeader(offVAGOffsets, 2 * 256, "VAG Pointer Table");

    u32 vagStartOffset = offVAGOffsets + 2 * 256;
    u32 vagOffset = 0;
    // VAG pointer entries store each compressed sample size divided by 8.

    for (u32 i = 1; i <= numVAGs; i++) {
      u32 vagSize = readShort(offVAGOffsets + i * 2) * 8;

      snprintf(name, 256, "VAG Size /8 #%u", i);
      vagOffsetHdr->addChild(offVAGOffsets + i * 2, 2, name);

      auto absoluteVagOffset = vagStartOffset + vagOffset;
      if (absoluteVagOffset + vagSize <= nEndOffset) {
        m_vagLocations.emplace_back(vagOffset, vagSize);
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
  size_t sampleIndex = 0;
  auto sampCollOffset = sampColl->offset();
  for (auto& vagLoc : m_vagLocations) {
    if (vagLoc.offset == 0 && vagLoc.size == 0)
      continue;

    if (sampleIndex >= sampColl->sampleCount())
      return false;

    auto sample = sampColl->sample(sampleIndex++);
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
                   u32 offset,
                   u32 length,
                   u32 bank,
                   u32 instrNum,
                   const string &name)
    : VGMInstr(instrSet, offset, length, bank, instrNum, name),
      masterVol(127) {
}

VabInstr::~VabInstr() {
}


bool VabInstr::loadInstr() {
  s8 numRgns = attr.tones;
  for (int i = 0; i < numRgns; i++) {
    auto rgn = std::make_unique<VabRgn>(this, offset() + i * 0x20);
    if (!rgn->loadRgn()) {
      continue;
    }
    sinkRgn(std::move(rgn));
  }
  return true;
}

// ******
// VabRgn
// ******

VabRgn::VabRgn(VabInstr *instr, u32 offset)
    : VGMRgn(instr, offset) {
}

bool VabRgn::loadRgn() {
  VabInstr *instr = (VabInstr *) parInstr;
  Vab *vab = static_cast<Vab *>(instr->parInstrSet);
  setLength(0x20);
  readBytes(offset(), 0x20, &attr);

  addGeneralItem(offset(), 1, "Priority");
  addGeneralItem(offset() + 1, 1, "Mode (use reverb?)");
  const u8 toneVol = readByte(offset() + 2);
  const double combinedVolume =
    (static_cast<double>(vab->hdr.mvol) / 127.0) *
    (static_cast<double>(instr->masterVol) / 127.0) *
    (static_cast<double>(toneVol) / 127.0);
  // libsnd squares the final left/right voice volume after applying VAB master,
  // program, tone, and pan factors.
  addVolume(combinedVolume * combinedVolume, offset() + 2, 1);
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
  u8 ft = readByte(offset() + 5);
  ft = std::min(ft, static_cast<u8>(127));
  double cents = ft * 100.0 / 128.0;
  setFineTune(static_cast<s16>(cents));

  psxConvADSR<VabRgn>(this, ADSR1, ADSR2, false);
  return true;
}
