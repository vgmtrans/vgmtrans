#include "pch.h"
#include "Root.h"
#include "CPSScanner.h"
#include "CPSSeq.h"
#include "CPSInstr.h"
#include "MAMELoader.h"

using namespace std;

void CPSScanner::Scan(RawFile *file, void *info) {
  MAMEGame *gameentry = (MAMEGame *) info;
  CPSFormatVer fmt_ver = GetVersionEnum(gameentry->fmt_version_str);

  if (fmt_ver == VER_UNDEFINED) {
    string alert = "XML entry uses an undefined QSound version: " + gameentry->fmt_version_str;
    pRoot->AddLogItem(new LogItem(alert, LOG_LEVEL_ERR, "CPSScanner"));
    return;
  }

  switch (fmt_ver) {
    case VER_CPS1_200:
    case VER_CPS1_200ff:
    case VER_CPS1_350:
    case VER_CPS1_425:
    case VER_CPS1_500:
    case VER_CPS1_502:
      LoadCPS1(gameentry);
      return;
    default:
      break;
  }

  MAMERomGroup *seqRomGroupEntry = gameentry->GetRomGroupOfType("audiocpu");
  MAMERomGroup *sampsRomGroupEntry = gameentry->GetRomGroupOfType("qsound");
  if (!seqRomGroupEntry || !sampsRomGroupEntry)
    return;
  uint32_t seq_table_offset;
  uint32_t seq_table_length = 0;
  uint32_t instr_table_offset;
  uint32_t samp_table_offset;
  uint32_t samp_table_length = 0;
  uint32_t artic_table_offset = 0;
  uint32_t artic_table_length = 0x800;
  uint32_t num_instr_banks;
  if (!seqRomGroupEntry->file || !sampsRomGroupEntry->file ||
      !seqRomGroupEntry->GetHexAttribute("seq_table", &seq_table_offset) ||
      !seqRomGroupEntry->GetHexAttribute("samp_table", &samp_table_offset) ||
      !seqRomGroupEntry->GetAttribute("num_instr_banks", &num_instr_banks))
    return;
  seqRomGroupEntry->GetHexAttribute("samp_table_length", &samp_table_length);

  switch (fmt_ver) {
    case VER_100:
    case VER_101:
    case VER_103:
    case VER_104:
    case VER_105A:
    case VER_105C:
    case VER_105:
    case VER_106B:
    case VER_115C:
    case VER_115:
      if (!seqRomGroupEntry->GetHexAttribute("instr_table", &instr_table_offset))
        return;
      break;
    case VER_200:
    case VER_201B:
    case VER_CPS3:
      if (!seqRomGroupEntry->GetHexAttribute("instr_table_ptrs", &instr_table_offset))
        return;
      break;
    case VER_116B:
    case VER_116:
    case VER_130:
    case VER_131:
    case VER_140:
    case VER_171:
    case VER_180:
    case VER_210:
    case VER_211:
      if (!seqRomGroupEntry->GetHexAttribute("instr_table_ptrs", &instr_table_offset))
        return;
      if (fmt_ver >= VER_130 && !seqRomGroupEntry->GetHexAttribute("artic_table", &artic_table_offset))
        return;
      break;
  }

  CPSInstrSet *instrset = 0;
  CPSSampColl *sampcoll = 0;
  CPSSampleInfoTable *sampInfoTable = 0;
  CPSArticTable *articTable = 0;

  string artic_table_name;
  string instrset_name;
  string samp_info_table_name;
  string sampcoll_name;
  string seq_table_name;

  ostringstream name;
  name << gameentry->name.c_str() << " articulation table";
  artic_table_name = name.str();
  name.str("");
  name << gameentry->name.c_str() << " instrument set";
  instrset_name = name.str();
  name.str("");
  name << gameentry->name.c_str() << " sample collection";
  sampcoll_name = name.str();
  name.str("");
  name << gameentry->name.c_str() << " sample info table";
  samp_info_table_name = name.str();
  name.str("");
  name << gameentry->name.c_str() << " sequence pointer table";
  seq_table_name = name.str();


  RawFile *programFile = seqRomGroupEntry->file;
  RawFile *samplesFile = sampsRomGroupEntry->file;

  // LOAD INSTRUMENTS AND SAMPLES

  //fix because Vampire Savior sample table bleeds into artic table and no way to detect this
  if (!samp_table_length && (artic_table_offset > samp_table_offset))
    samp_table_length = artic_table_offset - samp_table_offset;
  if (fmt_ver < VER_CPS3) {
    sampInfoTable = new CPS2SampleInfoTable(programFile, samp_info_table_name, samp_table_offset, samp_table_length);
  }
  else {
    sampInfoTable = new CPS3SampleInfoTable(programFile, samp_info_table_name, samp_table_offset, samp_table_length);
  }
  sampInfoTable->LoadVGMFile();
  if (artic_table_offset) {
    articTable = new CPSArticTable(programFile, artic_table_name, artic_table_offset, artic_table_length);
    if (!articTable->LoadVGMFile()) {
      delete articTable;
      articTable = NULL;
    }
  }

  instrset = new CPSInstrSet(programFile,
                             fmt_ver,
                             instr_table_offset,
                             num_instr_banks,
                             sampInfoTable,
                             articTable,
                             instrset_name);
  if (!instrset->LoadVGMFile()) {
    delete instrset;
    instrset = NULL;
  }
  sampcoll = new CPSSampColl(samplesFile, instrset, sampInfoTable, 0, 0, sampcoll_name);
  if (!sampcoll->LoadVGMFile()) {
    delete sampcoll;
    sampcoll = NULL;
  }


  // LOAD SEQUENCES FROM SEQUENCE TABLE AND CREATE COLLECTIONS
  //  Set the seq table length to be the distance to the first seq pointer
  //  as sequences seem to immediately follow the seq table.
  unsigned int k = 0;
  uint32_t seqPointer = 0;
  while (seqPointer == 0)
    seqPointer = programFile->GetWordBE(seq_table_offset + (k++ * 4)) & 0x0FFFFF;
  if (fmt_ver == VER_CPS3) {
    seq_table_length = seqPointer - 8;
  }
  else {
    seq_table_length = seqPointer - seq_table_offset;
  }

  // Add SeqTable as Miscfile
  VGMMiscFile *seqTable = new VGMMiscFile(CPSFormat::name, seqRomGroupEntry->file, seq_table_offset, seq_table_length, seq_table_name);
  if (!seqTable->LoadVGMFile()) {
    delete seqTable;
    seqTable = NULL;
  }

  //HACK
  //k < 0x58 &&
  for (k = 0; (seq_table_length == 0 || k < seq_table_length); k += 4) {

    if (programFile->GetWordBE(seq_table_offset + k) == 0)
      continue;

    if (fmt_ver == VER_CPS3) {
      seqPointer = programFile->GetWordBE(seq_table_offset + k) + seq_table_offset - 8;
    }
    else {
      // & 0x0FFFFF because SSF2 sets 0x100000 for some reason
      seqPointer = programFile->GetWordBE(seq_table_offset + k) & 0x0FFFFF;
    }

    seqTable->AddSimpleItem(seq_table_offset + k, 4, "Sequence Pointer");

    name.str("");
    name << gameentry->name.c_str() << " song " << k / 4;
    VGMColl *coll = new VGMColl(name.str());
    name.str("");
    name << gameentry->name.c_str() << " seq " << k / 4;
    string seqName = name.str();
    CPSSeq *newSeq = new CPSSeq(programFile, seqPointer, fmt_ver, seqName);
    if (newSeq->LoadVGMFile()) {
      coll->UseSeq(newSeq);
      coll->AddInstrSet(instrset);
      coll->AddSampColl(sampcoll);
      coll->AddMiscFile(sampInfoTable);
      if (articTable)
        coll->AddMiscFile(articTable);
      if (!coll->Load()) {
        delete coll;
      }
    }
    else {
      delete newSeq;
      delete coll;
    }
  }

  return;
}

void CPSScanner::LoadCPS1(MAMEGame *gameentry) {

  CPSFormatVer fmt_ver = GetVersionEnum(gameentry->fmt_version_str);

  CPS1SampleInstrSet *instrset = nullptr;
  CPS1SampColl *sampcoll = nullptr;

  MAMERomGroup* seqRomGroupEntry = gameentry->GetRomGroupOfType("audiocpu");
  if (!seqRomGroupEntry)
    return;
  uint32_t seq_table_offset;
  uint32_t seq_table_length;
  if (!seqRomGroupEntry->file ||
      !seqRomGroupEntry->GetHexAttribute("seq_table", &seq_table_offset))
    return;

  if (fmt_ver == VER_UNDEFINED) {
    string alert = "XML entry uses an undefined QSound version: " + gameentry->fmt_version_str;
    pRoot->AddLogItem(new LogItem(alert, LOG_LEVEL_ERR, "CPSScanner"));
    return;
  }

  RawFile *programFile = seqRomGroupEntry->file;

  MAMERomGroup* sampsRomGroupEntry = gameentry->GetRomGroupOfType("oki6295");
  if (sampsRomGroupEntry && sampsRomGroupEntry->file) {
    uint32_t instrTablePtrOffset = 4;
    uint32_t instr_table_offset = programFile->GetShortBE(seq_table_offset + instrTablePtrOffset);

    RawFile *samplesFile = sampsRomGroupEntry->file;

    ostringstream name;
    name.str("");
    name << gameentry->name.c_str() << " oki msm6295 instrument set";
    auto instrset_name = name.str();
    name.str("");
    name << gameentry->name.c_str() << " sample collection";
    auto sampcoll_name = name.str();

    instrset = new CPS1SampleInstrSet(programFile,
                               fmt_ver,
                               instr_table_offset,
                               instrset_name);
    if (!instrset->LoadVGMFile()) {
      delete instrset;
      instrset = NULL;
    }

    sampcoll = new CPS1SampColl(samplesFile, instrset, 0, samplesFile->size(), sampcoll_name);
    if (!sampcoll->LoadVGMFile()) {
      delete sampcoll;
      sampcoll = NULL;
    }
  }

  string seq_table_name;
  ostringstream name;

  name.str("");
  name << gameentry->name.c_str() << " sequence pointer table";
  seq_table_name = name.str();

  uint8_t ptrsStart;
  const uint8_t ptrSize = 2;

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
      seq_table_length = (uint32_t) (programFile->GetByte(seq_table_offset) * 2) + ptrsStart;
      break;
    case VER_CPS1_502:
      seq_table_length = (uint32_t) (programFile->GetShortBE(seq_table_offset) * 2) + ptrsStart;
      break;
  }

  unsigned int k = 0;
  uint32_t seqPointer = 0;

  // Add SeqTable as Miscfile
  VGMMiscFile *seqTable = new VGMMiscFile(CPSFormat::name, seqRomGroupEntry->file, seq_table_offset, seq_table_length, seq_table_name);
  if (!seqTable->LoadVGMFile()) {
    delete seqTable;
    seqTable = NULL;
  }

  int seqNum = 0;
  for (k = ptrsStart; (seq_table_length == 0 || k < seq_table_length); k += ptrSize) {

    seqPointer = programFile->GetShortBE(seq_table_offset + k);

    if (seqPointer == 0) {
      continue;
    }

    seqTable->AddSimpleItem(seq_table_offset + k, ptrSize, "Sequence Pointer");

    name.str("");
    name << gameentry->name.c_str() << " seq " << seqNum;
    string seqName = name.str();
    CPSSeq *newSeq = new CPSSeq(programFile, seqPointer, fmt_ver, seqName);

    if (!newSeq->LoadVGMFile()) {
      delete newSeq;
      continue;
    }

    name.str("");
    name << gameentry->name.c_str() << " song " << seqNum++;
    VGMColl* coll = new VGMColl(name.str());

    coll->UseSeq(newSeq);
    coll->AddInstrSet(instrset);
    coll->AddSampColl(sampcoll);
    if (!coll->Load()) {
      delete coll;
    }
  }
}

CPSFormatVer CPSScanner::GetVersionEnum(string &versionStr) {
  if (versionStr == "CPS1_2.00") return VER_CPS1_200;
  if (versionStr == "CPS1_2.00ff") return VER_CPS1_200ff;
  if (versionStr == "CPS1_3.50") return VER_CPS1_350;
  if (versionStr == "CPS1_4.25") return VER_CPS1_425;
  if (versionStr == "CPS1_5.00") return VER_CPS1_500;
  if (versionStr == "CPS1_5.02") return VER_CPS1_502;
  if (versionStr == "1.00") return VER_100;
  if (versionStr == "1.01") return VER_101;
  if (versionStr == "1.03") return VER_103;
  if (versionStr == "1.04") return VER_104;
  if (versionStr == "1.05a") return VER_105A;
  if (versionStr == "1.05c") return VER_105C;
  if (versionStr == "1.05") return VER_105;
  if (versionStr == "1.06b") return VER_106B;
  if (versionStr == "1.15c") return VER_115C;
  if (versionStr == "1.15") return VER_115;
  if (versionStr == "1.16b") return VER_116B;
  if (versionStr == "1.16") return VER_116;
  if (versionStr == "1.30") return VER_130;
  if (versionStr == "1.31") return VER_131;
  if (versionStr == "1.40") return VER_140;
  if (versionStr == "1.71") return VER_171;
  if (versionStr == "1.80") return VER_180;
  if (versionStr == "2.00") return VER_200;
  if (versionStr == "2.01b") return VER_201B;
  if (versionStr == "2.10") return VER_210;
  if (versionStr == "2.11") return VER_211;
  if (versionStr == "CPS3") return VER_CPS3;
  return VER_UNDEFINED;
}
