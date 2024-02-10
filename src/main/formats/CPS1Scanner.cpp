#include "pch.h"
#include "Root.h"
#include "CPS1Scanner.h"
#include "CPSSeq.h"
#include "CPS1Instr.h"
#include "MAMELoader.h"
#include "VGMMiscFile.h"

using namespace std;

class CPS1SampleInstrSet;

void CPS1Scanner::Scan(RawFile *file, void *info) {
  MAMEGame *gameentry = (MAMEGame *) info;
  CPSFormatVer fmt_ver = GetVersionEnum(gameentry->fmt_version_str);

  if (fmt_ver == VER_UNDEFINED) {
    string alert = "XML entry uses an undefined format version: " + gameentry->fmt_version_str;
    pRoot->AddLogItem(new LogItem(alert, LOG_LEVEL_ERR, "CPS1Scanner"));
    return;
  }

  switch (fmt_ver) {
    case VER_CPS1_200:
    case VER_CPS1_200ff:
    case VER_CPS1_350:
    case VER_CPS1_425:
    case VER_CPS1_500:
    case VER_CPS1_502:
      LoadCPS1(gameentry, fmt_ver);
      break;
    default:
      break;
  }
}

void CPS1Scanner::LoadCPS1(MAMEGame *gameentry, CPSFormatVer fmt_ver) {
  CPS1OPMInstrSet *opmInstrset = nullptr;
  CPS1SampleInstrSet *sampleInstrset = nullptr;
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
    pRoot->AddLogItem(new LogItem(alert, LOG_LEVEL_ERR, "CPS1Scanner"));
    return;
  }

  RawFile *programFile = seqRomGroupEntry->file;

  // Load the YM2151 Instrument Set
  const uint32_t opmInstrTablePtrOffset = 2;
  uint32_t opm_instr_table_offset = programFile->GetShortBE(seq_table_offset + opmInstrTablePtrOffset);

  ostringstream name;
  name.str("");
  name << gameentry->name.c_str() << " YM2151 instrument set";
  auto instrset_name = name.str();

  opmInstrset = new CPS1OPMInstrSet(programFile,
                                    fmt_ver, opm_instr_table_offset,
                                    instrset_name);
  if (!opmInstrset->LoadVGMFile()) {
    delete opmInstrset;
    opmInstrset = NULL;
  }

  MAMERomGroup* sampsRomGroupEntry = gameentry->GetRomGroupOfType("oki6295");
  if (sampsRomGroupEntry && sampsRomGroupEntry->file) {
    const uint32_t sampleInstrTablePtrOffset = 4;
    uint32_t sample_instr_table_offset = programFile->GetShortBE(seq_table_offset + sampleInstrTablePtrOffset);

    RawFile *samplesFile = sampsRomGroupEntry->file;

    ostringstream name;
    name.str("");
    name << gameentry->name.c_str() << " oki msm6295 instrument set";
    auto instrset_name = name.str();
    name.str("");
    name << gameentry->name.c_str() << " sample collection";
    auto sampcoll_name = name.str();

    sampleInstrset = new CPS1SampleInstrSet(programFile,
                                      fmt_ver, sample_instr_table_offset,
                                      instrset_name);
    if (!sampleInstrset->LoadVGMFile()) {
      delete sampleInstrset;
      sampleInstrset = NULL;
    }

    sampcoll = new CPS1SampColl(samplesFile, sampleInstrset, 0, samplesFile->size(), sampcoll_name);
    if (!sampcoll->LoadVGMFile()) {
      delete sampcoll;
      sampcoll = NULL;
    }
  }

  string seq_table_name;

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
  VGMMiscFile *seqTable = new VGMMiscFile(CPS1Format::name, seqRomGroupEntry->file, seq_table_offset, seq_table_length, seq_table_name);
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

    if (opmInstrset && sampleInstrset && sampcoll) {
      name.str("");
      name << gameentry->name.c_str() << " song " << seqNum++;
      VGMColl* coll = new VGMColl(name.str());

      coll->UseSeq(newSeq);
      coll->AddInstrSet(opmInstrset);
      coll->AddInstrSet(sampleInstrset);
      coll->AddSampColl(sampcoll);
      if (!coll->Load()) {
        delete coll;
      }
    }
  }
}

