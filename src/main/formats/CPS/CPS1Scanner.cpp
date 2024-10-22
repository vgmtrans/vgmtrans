/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "common.h"
#include "CPS1Scanner.h"
#include "CPS1Seq.h"
#include "CPS2Seq.h"
#include "CPS1Instr.h"
#include "MAMELoader.h"
#include "VGMMiscFile.h"
#include "VGMColl.h"

class CPS1SampleInstrSet;

CPS1FormatVer cps1VersionEnum(const std::string &versionStr) {
  static const std::unordered_map<std::string, CPS1FormatVer> versionMap = {
    {"CPS1_V1.00", CPS1_V100},
    {"CPS1_V2.00", CPS1_V200},
    {"CPS1_V3.50", CPS1_V350},
    {"CPS1_V4.25", CPS1_V425},
    {"CPS1_V5.00", CPS1_V500},
    {"CPS1_V5.02", CPS1_V502},
  };

  auto it = versionMap.find(versionStr);
  return it != versionMap.end() ? it->second : CPS1_VERSION_UNDEFINED;
}

void CPS1Scanner::scan(RawFile *file, void *info) {
  MAMEGame *gameentry = static_cast<MAMEGame*>(info);
  CPS1FormatVer fmt_ver = cps1VersionEnum(gameentry->fmt_version_str);

  if (fmt_ver == CPS1_VERSION_UNDEFINED) {
    L_ERROR("XML entry uses an undefined format version: {}", gameentry->fmt_version_str);
    return;
  }

  switch (fmt_ver) {
    case CPS1_V100:
    case CPS1_V200:
    case CPS1_V350:
    case CPS1_V425:
    case CPS1_V500:
    case CPS1_V502:
      loadCPS1(gameentry, fmt_ver);
      break;
    default:
      break;
  }
}

void CPS1Scanner::loadCPS1(MAMEGame *gameentry, CPS1FormatVer fmt_ver) {
  CPS1OPMInstrSet *opmInstrset = nullptr;
  CPS1SampleInstrSet *sampleInstrset = nullptr;
  CPS1SampColl *sampcoll = nullptr;

  MAMERomGroup* seqRomGroupEntry = gameentry->getRomGroupOfType("audiocpu");
  if (!seqRomGroupEntry || !seqRomGroupEntry->file)
    return;
  RawFile *programFile = seqRomGroupEntry->file;
  u32 seq_table_offset;
  u32 seq_table_length;
  u32 opm_instr_table_offset;
  u32 opm_instr_table_length;
  u32 sample_instr_table_offset = 0;

  u32 tables_offset;
  if (!seqRomGroupEntry->getHexAttribute("tables", &tables_offset))
    return;

  u8 numSeqs;
  u8 masterVol;
  switch (fmt_ver) {
    case CPS1_V100:
      numSeqs = programFile->readByte(tables_offset + 0);
      masterVol = 0x7F;
      seq_table_offset = tables_offset + 3;
      opm_instr_table_offset = seq_table_offset + (numSeqs+1) * 2;
      break;
    case CPS1_V200:
      numSeqs = programFile->readByte(tables_offset + 0);
      masterVol = 0x7F;
      opm_instr_table_offset = programFile->readShortBE(tables_offset + 1);
      seq_table_offset = tables_offset + 3;
      break;
    case CPS1_V350:
      numSeqs = programFile->readByte(tables_offset + 0);
      masterVol = programFile->readByte(tables_offset + 1);
      opm_instr_table_offset = programFile->readShortBE(tables_offset + 2);
      seq_table_offset = tables_offset + 4;
      break;
    case CPS1_V425:
      numSeqs = programFile->readByte(tables_offset + 0);
      masterVol = programFile->readByte(tables_offset + 1);
      opm_instr_table_offset = programFile->readShortBE(tables_offset + 2);
      sample_instr_table_offset = programFile->readShortBE(tables_offset + 4);
      seq_table_offset = tables_offset + 6;
      break;
    case CPS1_V500: {
      opm_instr_table_length = programFile->readShortBE(tables_offset);
      opm_instr_table_offset = tables_offset + 2;
      u32 seq_info_offset = opm_instr_table_offset + opm_instr_table_length;
      numSeqs = programFile->readByte(seq_info_offset);
      masterVol = programFile->readByte(seq_info_offset + 1);
      seq_table_offset = seq_info_offset + 2;
      break;
    }
    case CPS1_V502: {
      opm_instr_table_length = programFile->readShortBE(tables_offset);
      opm_instr_table_offset = tables_offset + 2;
      u32 seq_info_offset = opm_instr_table_offset + opm_instr_table_length;
      numSeqs = programFile->readShortBE(seq_info_offset);
      masterVol = programFile->readByte(seq_info_offset + 2);
      seq_table_offset = seq_info_offset + 4;
      break;
    }
    default:
      L_ERROR("Unknown version of CPS1 format");
      return;
  }
  seq_table_length = numSeqs * sizeof(u16);

  // Load the YM2151 Instrument Set
  auto instrset_name = fmt::format("{} YM2151 instrument set", gameentry->name);
  int opmInstrsetLength;

  switch (fmt_ver) {
    case CPS1_V200:
      opmInstrsetLength = 127 * static_cast<u32>(sizeof(CPS1OPMInstrDataV2_00));
      opmInstrset = new CPS1OPMInstrSet(programFile,
                                        fmt_ver, masterVol, opm_instr_table_offset,
                                        opmInstrsetLength,
                                        instrset_name);
      break;

    case CPS1_V500:
    case CPS1_V502:
      opmInstrsetLength = 127 * static_cast<u32>(sizeof(CPS1OPMInstrDataV5_02));
      opmInstrset = new CPS1OPMInstrSet(programFile,
                                        fmt_ver, masterVol, opm_instr_table_offset,
                                        opmInstrsetLength,
                                        instrset_name);
      break;

    case CPS1_V100:
    case CPS1_V350:
      opmInstrsetLength = 127 * static_cast<u32>(sizeof(CPS1OPMInstrDataV4_25));
      opmInstrset = new CPS1OPMInstrSet(programFile,
                                      fmt_ver, masterVol, opm_instr_table_offset,
                                      opmInstrsetLength,
                                      instrset_name);
      break;
    case CPS1_V425:
      opmInstrsetLength = std::min(127 * static_cast<u32>(sizeof(CPS1OPMInstrDataV4_25)), sample_instr_table_offset);
      opmInstrset = new CPS1OPMInstrSet(programFile,
                                        fmt_ver, masterVol, opm_instr_table_offset,
                                        opmInstrsetLength,
                                        instrset_name);
      break;
  }
  if (!opmInstrset->loadVGMFile()) {
    delete opmInstrset;
    opmInstrset = nullptr;
  }

  MAMERomGroup* sampsRomGroupEntry = gameentry->getRomGroupOfType("oki6295");
  if (sampsRomGroupEntry && sampsRomGroupEntry->file) {
    RawFile *samplesFile = sampsRomGroupEntry->file;

    auto instrset_name = fmt::format("{} oki msm6295 instrument set", gameentry->name);
    auto sampcoll_name = fmt::format("{} sample collection", gameentry->name);

    sampleInstrset = new CPS1SampleInstrSet(programFile,
                                fmt_ver, sample_instr_table_offset,
                                instrset_name);
    if (!sampleInstrset->loadVGMFile()) {
      delete sampleInstrset;
      sampleInstrset = nullptr;
    }

    sampcoll = new CPS1SampColl(samplesFile, sampleInstrset, 0, static_cast<uint32_t>(samplesFile->size()), sampcoll_name);
    if (!sampcoll->loadVGMFile()) {
      delete sampcoll;
      sampcoll = nullptr;
    }
  }

  std::string seq_table_name = fmt::format("{} sequence pointer table", gameentry->name);

  // Add SeqTable as Miscfile
  VGMMiscFile *seqTable = new VGMMiscFile(CPS1Format::name, seqRomGroupEntry->file, seq_table_offset, seq_table_length, seq_table_name);
  if (!seqTable->loadVGMFile()) {
    delete seqTable;
    return;
  }

  // Create instrument transpose table
  std::vector<s8> instrTransposeTable;
  if (opmInstrset && fmt_ver == CPS1_V425 || opmInstrset && fmt_ver == CPS1_V350 ||
    opmInstrset && fmt_ver == CPS1_V100) {
    instrTransposeTable.reserve(opmInstrset->aInstrs.size());
    for (const auto instr : opmInstrset->aInstrs) {
      if (auto* opmInstr = dynamic_cast<CPS1OPMInstr<CPS1OPMInstrDataV4_25>*>(instr); opmInstr != nullptr) {
        instrTransposeTable.emplace_back(opmInstr->getTranspose());
      }
    }
  }

  for (u32 seqId = 0; seqId < numSeqs; ++seqId) {
    uint32_t seqPointer = (fmt_ver > CPS1_V100) ?
      programFile->readShortBE(seq_table_offset + (seqId * sizeof(u16))) :
      programFile->readShort(seq_table_offset + (seqId * sizeof(u16)));

    if (seqPointer == 0) {
      continue;
    }

    seqTable->addChild(seq_table_offset + (seqId * sizeof(u16)), sizeof(u16), "Sequence Pointer");

    auto seqName = fmt::format("{} seq {}", gameentry->name, seqId);
    VGMSeq* newSeq;
    newSeq = new CPS1Seq(programFile, seqPointer, fmt_ver, seqName, instrTransposeTable);

    if (!newSeq->loadVGMFile()) {
      delete newSeq;
      continue;
    }

    if (opmInstrset) {
      auto collName = fmt::format("{} song {}", gameentry->name, seqId);
      VGMColl* coll = new VGMColl(collName);

      coll->useSeq(newSeq);
      coll->addInstrSet(opmInstrset);
      if (sampleInstrset && sampcoll) {
        coll->addInstrSet(sampleInstrset);
        coll->addSampColl(sampcoll);
      }
      if (!coll->load()) {
        delete coll;
      }
    }
  }
}

