#include "pch.h"
#include "NamcoS2Scanner.h"
#include "NamcoS2Seq.h"
#include "NamcoS2C140Instr.h"
#include "MAMELoader.h"
#include "VGMColl.h"
#include "main/LogItem.h"

void NamcoS2Scanner::Scan(RawFile* file, void* info) {
  MAMEGame* gameentry = (MAMEGame *) info;

  MAMERomGroup* cpuRomGroup = gameentry->GetRomGroupOfType("audiocpu");
  MAMERomGroup* pcmRomGroup = gameentry->GetRomGroupOfType("pcm");

  if (!cpuRomGroup || !pcmRomGroup) {
    return;
  }

  uint32_t c140BankOffset;
  uint32_t ym2151BankOffset;

  if (!cpuRomGroup ||
      !cpuRomGroup->file ||
      !cpuRomGroup->GetHexAttribute("c140_bank", &c140BankOffset) ||
      !cpuRomGroup->GetHexAttribute("ym2151_bank", &ym2151BankOffset)) {
    return;
  }

  shared_ptr<InstrMap> instrMap = make_shared<InstrMap>();
  auto seqs = LoadSequences(cpuRomGroup, c140BankOffset, ym2151BankOffset, instrMap);
  auto sampColl = LoadSampColl(cpuRomGroup, pcmRomGroup, c140BankOffset);
  if (!sampColl)
    return;
  auto instrSet = LoadInstruments(cpuRomGroup, c140BankOffset, sampColl, ym2151BankOffset, instrMap);
  if (!instrSet)
    return;

  wostringstream name;
  uint8_t i=0;
  for (auto seq: seqs) {
    name.str(L"");
    name << gameentry->name.c_str() << L" c140 seq " << i++;

    auto coll = new VGMColl(name.str());
    coll->UseSeq(seq);
    coll->AddInstrSet(instrSet);
    coll->AddSampColl(sampColl);
    if (!coll->Load())
      delete coll;
  }
}

vector<NamcoS2Seq*> NamcoS2Scanner::LoadSequences(
    MAMERomGroup* cpuRomGroup,
    uint32_t c140BankOffset,
    uint32_t ym2151BankOffset,
    shared_ptr<InstrMap> instrMap
) {

  RawFile* codeFile = cpuRomGroup->file;
  wostringstream name;

  auto seqs = vector<NamcoS2Seq*>();

  uint16_t sampTableOffset = codeFile->GetShortBE(c140BankOffset + 2);
  uint16_t seqTableOffset = codeFile->GetShortBE(c140BankOffset);
  uint16_t seqIndex = 0;
  uint16_t seqPtr = codeFile->GetShortBE(c140BankOffset + seqTableOffset);
  while (seqPtr != 0 &&
      seqPtr > sampTableOffset &&
      seqIndex < 0x100) {

    name.str(L"");
    name << L"NamcoS2Seq " << seqIndex;
    wstring seqName = name.str();

    NamcoS2Seq *seq = new NamcoS2Seq(codeFile, c140BankOffset, ym2151BankOffset, seqIndex++, seqPtr, instrMap, seqName);
    if (!seq->LoadVGMFile()) {
      delete seq;
    } else {
      seqs.push_back(seq);
    }
    seqPtr = codeFile->GetShortBE(c140BankOffset + seqTableOffset + seqIndex * 2);
  }
  return seqs;
}

NamcoS2SampColl* NamcoS2Scanner::LoadSampColl(
    MAMERomGroup* cpuRomGroup,
    MAMERomGroup* pcmRomGroup,
    uint32_t c140BankOffset
  ) {

  RawFile *samplesFile = pcmRomGroup->file;
  RawFile *codeFile = cpuRomGroup->file;

  uint32_t sampleTableOffset = c140BankOffset + codeFile->GetShortBE(c140BankOffset+2);
//  uint8_t sampleCount = 0x5A;// 0x3D;//codeFile->GetByte(c140BankOffset+7);

  auto sampInfoTable = new NamcoS2SampleInfoTable(codeFile, sampleTableOffset, samplesFile->size());
  sampInfoTable->LoadVGMFile();

  auto sampColl = new NamcoS2SampColl(samplesFile, sampInfoTable, 0, 0);
  if (!sampColl->LoadVGMFile()) {
    delete sampColl;
    return NULL;
  }
  return sampColl;
}

NamcoS2C140InstrSet* NamcoS2Scanner::LoadInstruments(
    MAMERomGroup* cpuRomGroup,
    uint32_t c140BankOffset,
    NamcoS2SampColl* sampColl,
    uint32_t ym2151BankOffset,
    shared_ptr<InstrMap> instrMap
) {

  RawFile *codeFile = cpuRomGroup->file;

  // Add Artic Table
  uint32_t articTableOffset = c140BankOffset + codeFile->GetShortBE(c140BankOffset+10);
  auto articTable = new NamcoS2ArticTable(codeFile, articTableOffset, 0, c140BankOffset);
  articTable->LoadVGMFile();

  auto instrSet = new NamcoS2C140InstrSet(codeFile, 0, sampColl, articTable, instrMap);
  if (!instrSet->LoadVGMFile()) {
    delete instrSet;
    return NULL;
  }
  return instrSet;
}