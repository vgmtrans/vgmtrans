/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "Root.h"
#include "CPS2Scanner.h"
#include "CPS2Seq.h"
#include "CPS2Instr.h"
#include "MAMELoader.h"
#include "CPS2Format.h"
#include "VGMColl.h"

CPS2FormatVer cps2VersionEnum(const std::string &versionStr) {
    static const std::unordered_map<std::string, CPS2FormatVer> versionMap = {
        {"CPS2_V1.00", CPS2_V100},
        {"CPS2_V1.01", CPS2_V101},
        {"CPS2_V1.03", CPS2_V103},
        {"CPS2_V1.04", CPS2_V104},
        {"CPS2_V1.05A", CPS2_V105A},
        {"CPS2_V1.05C", CPS2_V105C},
        {"CPS2_V1.05", CPS2_V105},
        {"CPS2_V1.06B", CPS2_V106B},
        {"CPS2_V1.15C", CPS2_V115C},
        {"CPS2_V1.15", CPS2_V115},
        {"CPS2_V1.16B", CPS2_V116B},
        {"CPS2_V1.16", CPS2_V116},
        {"CPS2_V1.30", CPS2_V130},
        {"CPS2_V1.31", CPS2_V131},
        {"CPS2_V1.40", CPS2_V140},
        {"CPS2_V1.71", CPS2_V171},
        {"CPS2_V1.80", CPS2_V180},
        {"CPS2_V2.00", CPS2_V200},
        {"CPS2_V2.01B", CPS2_V201B},
        {"CPS2_V2.10", CPS2_V210},
        {"CPS2_V2.11", CPS2_V211},
        {"CPS3", CPS3}
    };

    auto it = versionMap.find(versionStr);
    return it != versionMap.end() ? it->second : CPS2_VERSION_UNDEFINED;
}

void CPS2Scanner::scan(RawFile* /*file*/, void* info) {
  MAMEGame *gameentry = static_cast<MAMEGame*>(info);
  CPS2FormatVer fmt_ver = cps2VersionEnum(gameentry->fmt_version_str);

  if (fmt_ver == CPS2_VERSION_UNDEFINED) {
    L_ERROR("XML entry uses an undefined QSound version: {}", gameentry->fmt_version_str);
    return;
  }

  MAMERomGroup *seqRomGroupEntry = gameentry->getRomGroupOfType("audiocpu");
  MAMERomGroup *sampsRomGroupEntry = gameentry->getRomGroupOfType("qsound");
  if (!seqRomGroupEntry || !sampsRomGroupEntry)
    return;
  uint32_t seq_table_offset;
  uint32_t seq_table_length;
  uint32_t instr_table_offset;
  uint32_t samp_table_offset;
  uint32_t samp_table_length = 0;
  uint32_t artic_table_offset = 0;
  uint32_t artic_table_length = 0x800;
  uint32_t num_instr_banks;
  if (!seqRomGroupEntry->file || !sampsRomGroupEntry->file ||
      !seqRomGroupEntry->getHexAttribute("seq_table", &seq_table_offset) ||
      !seqRomGroupEntry->getHexAttribute("samp_table", &samp_table_offset) ||
      !seqRomGroupEntry->getAttribute("num_instr_banks", &num_instr_banks))
    return;
  seqRomGroupEntry->getHexAttribute("samp_table_length", &samp_table_length);

  switch (fmt_ver) {
    case CPS2_V100:
    case CPS2_V101:
    case CPS2_V103:
    case CPS2_V104:
    case CPS2_V105A:
    case CPS2_V105C:
    case CPS2_V105:
    case CPS2_V106B:
    case CPS2_V115C:
    case CPS2_V115:
      if (!seqRomGroupEntry->getHexAttribute("instr_table", &instr_table_offset))
        return;
      break;
    case CPS2_V200:
    case CPS2_V201B:
    case CPS3:
      if (!seqRomGroupEntry->getHexAttribute("instr_table_ptrs", &instr_table_offset))
        return;
      break;
    case CPS2_V116B:
    case CPS2_V116:
    case CPS2_V130:
    case CPS2_V131:
    case CPS2_V140:
    case CPS2_V171:
    case CPS2_V180:
    case CPS2_V210:
    case CPS2_V211:
      if (!seqRomGroupEntry->getHexAttribute("instr_table_ptrs", &instr_table_offset))
        return;
      if (fmt_ver >= CPS2_V130 && !seqRomGroupEntry->getHexAttribute("artic_table", &artic_table_offset))
        return;
      break;
    default:
      L_ERROR("Unknown version of CPS2 format: {}", static_cast<uint8_t>(fmt_ver));
      return;
  }

  CPS2InstrSet *instrset;
  CPS2SampColl *sampcoll;
  CPSSampleInfoTable *sampInfoTable;
  CPSArticTable *articTable = nullptr;

  std::string artic_table_name;
  std::string instrset_name;
  std::string samp_info_table_name;
  std::string sampcoll_name;
  std::string seq_table_name;

  artic_table_name = fmt::format("{} articulation table", gameentry->name);
  instrset_name = fmt::format("{} instrument table", gameentry->name);
  sampcoll_name = fmt::format("{} sample collection", gameentry->name);
  samp_info_table_name = fmt::format("{} sample info table", gameentry->name);
  seq_table_name =fmt::format("{} sequence pointer table", gameentry->name);

  RawFile *programFile = seqRomGroupEntry->file;
  RawFile *samplesFile = sampsRomGroupEntry->file;

  if (fmt_ver == CPS3) {
    sampInfoTable = new CPS3SampleInfoTable(programFile, samp_info_table_name, samp_table_offset, samp_table_length);
  }
  else {
    sampInfoTable = new CPS2SampleInfoTable(programFile, samp_info_table_name, samp_table_offset, samp_table_length);
  }
  sampInfoTable->loadVGMFile();
  if (artic_table_offset) {
    articTable = new CPSArticTable(programFile, artic_table_name, artic_table_offset, artic_table_length);
    if (!articTable->loadVGMFile()) {
      delete articTable;
      articTable = nullptr;
    }
  }

  instrset = new CPS2InstrSet(programFile,
                             fmt_ver,
                             instr_table_offset,
                             num_instr_banks,
                             sampInfoTable,
                             articTable,
                             instrset_name);
  if (!instrset->loadVGMFile()) {
    delete instrset;
    instrset = nullptr;
  }
  sampcoll = new CPS2SampColl(samplesFile, instrset, sampInfoTable, 0, 0, sampcoll_name);
  if (!sampcoll->loadVGMFile()) {
    delete sampcoll;
    sampcoll = nullptr;
  }


  // LOAD SEQUENCES FROM SEQUENCE TABLE AND CREATE COLLECTIONS
  //  Set the seq table length to be the distance to the first seq pointer
  //  as sequences seem to immediately follow the seq table.
  unsigned int k = 0;
  uint32_t seqPointer = 0;
  while (seqPointer == 0)
    seqPointer = programFile->readWordBE(seq_table_offset + (k++ * 4)) & 0x0FFFFF;
  if (fmt_ver == CPS3) {
    seq_table_length = seqPointer - 8;
  }
  else {
    seq_table_length = seqPointer - seq_table_offset;
  }

  // Add SeqTable as Miscfile
  VGMMiscFile *seqTable = new VGMMiscFile(CPS2Format::name, seqRomGroupEntry->file, seq_table_offset, seq_table_length, seq_table_name);
  if (!seqTable->loadVGMFile()) {
    delete seqTable;
    return;
  }

  //HACK
  //k < 0x58 &&
  for (k = 0; (seq_table_length == 0 || k < seq_table_length); k += 4) {

    if (programFile->readWordBE(seq_table_offset + k) == 0)
      continue;

    if (fmt_ver == CPS3) {
      seqPointer = programFile->readWordBE(seq_table_offset + k) + seq_table_offset - 8;
    }
    else {
      // & 0x0FFFFF because SSF2 sets 0x100000 for some reason
      seqPointer = programFile->readWordBE(seq_table_offset + k) & 0x0FFFFF;
    }

    seqTable->addChild(seq_table_offset + k, 4, "Sequence Pointer");

    std::string collName = fmt::format("{} song {}", gameentry->name, k / 4);
    VGMColl *coll = new VGMColl(collName);
    std::string seqName = fmt::format("{} seq {}", gameentry->name, k / 4);
    CPS2Seq *newSeq = new CPS2Seq(programFile, seqPointer, fmt_ver, seqName);
    if (newSeq->loadVGMFile()) {
      coll->useSeq(newSeq);
      coll->addInstrSet(instrset);
      coll->addSampColl(sampcoll);
      coll->addMiscFile(sampInfoTable);
      if (articTable)
        coll->addMiscFile(articTable);
      if (!coll->load()) {
        delete coll;
      }
    } else {
      delete newSeq;
      delete coll;
    }
  }

  return;
}
