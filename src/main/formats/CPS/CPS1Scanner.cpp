/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "Root.h"
#include "CPS1Scanner.h"
#include "CPSSeq.h"
#include "CPS1Instr.h"
#include "MAMELoader.h"
#include "VGMMiscFile.h"
#include "VGMColl.h"

class CPS1SampleInstrSet;

void CPS1Scanner::scan(RawFile *file, void *info) {
  MAMEGame *gameentry = static_cast<MAMEGame*>(info);
  CPSFormatVer fmt_ver = versionEnum(gameentry->fmt_version_str);

  if (fmt_ver == VER_UNDEFINED) {
    L_ERROR("XML entry uses an undefined format version: {}", gameentry->fmt_version_str);
    return;
  }

  switch (fmt_ver) {
    case VER_CPS1_200:
    case VER_CPS1_200ff:
    case VER_CPS1_350:
    case VER_CPS1_425:
    case VER_CPS1_500:
    case VER_CPS1_502:
      loadCPS1(gameentry, fmt_ver);
      break;
    default:
      break;
  }
}

void CPS1Scanner::loadCPS1(MAMEGame *gameentry, CPSFormatVer fmt_ver) {
  CPS1SampleInstrSet *instrset = nullptr;
  CPS1SampColl *sampcoll = nullptr;

  MAMERomGroup* seqRomGroupEntry = gameentry->getRomGroupOfType("audiocpu");
  if (!seqRomGroupEntry)
    return;
  uint32_t seq_table_offset;
  uint32_t seq_table_length;
  if (!seqRomGroupEntry->file ||
      !seqRomGroupEntry->getHexAttribute("seq_table", &seq_table_offset))
    return;

  RawFile *programFile = seqRomGroupEntry->file;

  MAMERomGroup* sampsRomGroupEntry = gameentry->getRomGroupOfType("oki6295");
  if (sampsRomGroupEntry && sampsRomGroupEntry->file) {
    uint32_t instrTablePtrOffset = 4;
    uint32_t instr_table_offset = programFile->readShortBE(seq_table_offset + instrTablePtrOffset);

    RawFile *samplesFile = sampsRomGroupEntry->file;

    auto instrset_name = fmt::format("{} oki msm6295 instrument set", gameentry->name);
    auto sampcoll_name = fmt::format("{} sample collection", gameentry->name);

    instrset = new CPS1SampleInstrSet(programFile,
                                      fmt_ver,
                                      instr_table_offset,
                                      instrset_name);
    if (!instrset->loadVGMFile()) {
      delete instrset;
      instrset = nullptr;
    }

    sampcoll = new CPS1SampColl(samplesFile, instrset, 0, static_cast<uint32_t>(samplesFile->size()), sampcoll_name);
    if (!sampcoll->loadVGMFile()) {
      delete sampcoll;
      sampcoll = nullptr;
    }
  }

  std::string seq_table_name = fmt::format("{} sequence pointer table", gameentry->name);

  uint8_t ptrsStart;
  constexpr uint8_t ptrSize = 2;

  switch (fmt_ver) {
    case VER_CPS1_500: ptrsStart = 2; break;
    case VER_CPS1_200ff: ptrsStart = 3; break;
    default: ptrsStart = 4;
  }

  switch (fmt_ver) {
    case VER_CPS1_200:
    case VER_CPS1_200ff:
    case VER_CPS1_350:
    case VER_CPS1_425:
    case VER_CPS1_500:
      seq_table_length = static_cast<uint32_t>(programFile->readByte(seq_table_offset) * 2) + ptrsStart;
      break;
    case VER_CPS1_502:
      seq_table_length = static_cast<uint32_t>(programFile->readShortBE(seq_table_offset) * 2) + ptrsStart;
      break;
    default:
      L_ERROR("Unknown version of CPS1 format: {}", static_cast<uint8_t>(fmt_ver));
      return;
  }

  unsigned int k;
  uint32_t seqPointer;

  // Add SeqTable as Miscfile
  VGMMiscFile *seqTable = new VGMMiscFile(CPS1Format::name, seqRomGroupEntry->file, seq_table_offset, seq_table_length, seq_table_name);
  if (!seqTable->loadVGMFile()) {
    delete seqTable;
    return;
  }

  int seqNum = 0;
  for (k = ptrsStart; (seq_table_length == 0 || k < seq_table_length); k += ptrSize) {

    seqPointer = programFile->readShortBE(seq_table_offset + k);

    if (seqPointer == 0) {
      continue;
    }

    seqTable->addChild(seq_table_offset + k, ptrSize, "Sequence Pointer");

    auto seqName = fmt::format("{} seq {}", gameentry->name, seqNum);
    CPSSeq *newSeq = new CPSSeq(programFile, seqPointer, fmt_ver, seqName);

    if (!newSeq->loadVGMFile()) {
      delete newSeq;
      continue;
    }

    auto collName = fmt::format("{} song {}", gameentry->name, seqNum++);
    VGMColl* coll = new VGMColl(collName);

    coll->useSeq(newSeq);
    coll->addInstrSet(instrset);
    coll->addSampColl(sampcoll);
    if (!coll->load()) {
      delete coll;
    }
  }
}

