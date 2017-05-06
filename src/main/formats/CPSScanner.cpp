#include "pch.h"
#include "CPSScanner.h"
#include "CPSSeq.h"
#include "CPSInstr.h"
#include "MAMELoader.h"
#include "main/LogItem.h"

using namespace std;

//#define PROG_INFO_TABLE_OFFSET 0x7000
//#define qs_samp_info_TABLE_OFFSET 0x8000

void CPSScanner::Scan(RawFile *file, void *info) {
  MAMEGameEntry *gameentry = (MAMEGameEntry *) info;
  MAMERomGroupEntry *seqRomGroupEntry = gameentry->GetRomGroupOfType("audiocpu");
  MAMERomGroupEntry *sampsRomGroupEntry = gameentry->GetRomGroupOfType("qsound");
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

  CPSFormatVer fmt_ver = GetVersionEnum(gameentry->fmt_version_str);
  if (fmt_ver == VER_UNDEFINED) {
    wstring alert = L"XML entry uses an undefined QSound version: " + string2wstring(gameentry->fmt_version_str);
    core.AddLogItem(new LogItem(alert, LOG_LEVEL_ERR, L"CPSScanner"));
    return;
  }

  switch (fmt_ver) {
    case VER_100 ... VER_115:
      if (!seqRomGroupEntry->GetHexAttribute("instr_table", &instr_table_offset))
        return;
      break;
    case VER_201B:
    case VER_CPS3:
      if (!seqRomGroupEntry->GetHexAttribute("instr_table_ptrs", &instr_table_offset))
        return;
      break;
    case VER_116B ... VER_180:
    case VER_210:
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

  wstring artic_table_name;
  wstring instrset_name;
  wstring samp_info_table_name;
  wstring sampcoll_name;
  wstring seq_table_name;

  wostringstream name;
  name << gameentry->name.c_str() << L" articulation table";
  artic_table_name = name.str();
  name.str(L"");
  name << gameentry->name.c_str() << L" instrument set";
  instrset_name = name.str();
  name.str(L"");
  name << gameentry->name.c_str() << L" sample collection";
  sampcoll_name = name.str();
  name.str(L"");
  name << gameentry->name.c_str() << L" sample info table";
  samp_info_table_name = name.str();
  name.str(L"");
  name << gameentry->name.c_str() << L" sequence pointer table";
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

    seqTable->AddSimpleItem(seq_table_offset + k, 4, L"Sequence Pointer");

    name.str(L"");
    name << gameentry->name.c_str() << L" song " << k / 4;
    VGMColl *coll = new VGMColl(name.str());
    name.str(L"");
    name << gameentry->name.c_str() << L" seq " << k / 4;
    wstring seqName = name.str();
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

CPSFormatVer CPSScanner::GetVersionEnum(string &versionStr) {
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
  if (versionStr == "2.01b") return VER_201B;
  if (versionStr == "2.10") return VER_210;
  if (versionStr == "CPS3") return VER_CPS3;
  return VER_UNDEFINED;
}
