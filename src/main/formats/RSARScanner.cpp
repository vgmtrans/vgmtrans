#include "pch.h"

#include "RSARFormat.h"
#include "RSARScanner.h"
#include "RSARSeq.h"
#include "RSARInstrSet.h"

DECLARE_FORMAT(RSAR);

/* RSAR */

std::vector<std::string> RSAR::ParseSymbBlock(uint32_t blockBase) {
  uint32_t strTableBase = blockBase + file->GetWordBE(blockBase);
  uint32_t strTableCount = file->GetWordBE(strTableBase + 0x00);

  std::vector<std::string> table(strTableCount);
  uint32_t strTableIdx = strTableBase + 0x04;
  for (uint32_t i = 0; i < strTableCount; i++) {
    uint32_t strOffs = file->GetWordBE(strTableIdx);
    strTableIdx += 0x04;

    uint8_t *strP = &file->buf.data[blockBase + strOffs];
    std::string str((char *)strP);
    table[i] = str;
  }

  return table;
}

std::vector<RSAR::Sound> RSAR::ReadSoundTable(uint32_t infoBlockOffs) {
  uint32_t soundTableOffs = file->GetWordBE(infoBlockOffs + 0x04);
  uint32_t soundTableCount = file->GetWordBE(infoBlockOffs + soundTableOffs);
  std::vector<Sound> soundTable;

  uint32_t soundTableIdx = infoBlockOffs + soundTableOffs + 0x08;
  for (uint32_t i = 0; i < soundTableCount; i++) {
    uint32_t soundBase = infoBlockOffs + file->GetWordBE(soundTableIdx);
    soundTableIdx += 0x08;

    Sound sound;
    uint32_t stringID = file->GetWordBE(soundBase + 0x00);
    if (stringID != 0xFFFFFFFF)
      sound.name = stringTable[stringID];
    sound.fileID = file->GetWordBE(soundBase + 0x04);
    /* 0x08 player */
    /* 0x0C param3DRef */
    /* 0x14 volume */
    /* 0x15 playerPriority */
    sound.type = (Sound::Type) file->GetByte(soundBase + 0x16);
    if (sound.type != Sound::Type::SEQ)
      continue;

    /* 0x17 remoteFilter */
    uint32_t seqInfoOffs = infoBlockOffs + file->GetWordBE(soundBase + 0x1C);

    sound.seq.dataOffset = file->GetWordBE(seqInfoOffs + 0x00);
    sound.seq.bankID = file->GetWordBE(seqInfoOffs + 0x04);
    sound.seq.allocTrack = file->GetWordBE(seqInfoOffs + 0x08);
    soundTable.push_back(sound);
  }

  return soundTable;
}

std::vector<RSAR::Bank> RSAR::ReadBankTable(uint32_t infoBlockOffs) {
  uint32_t bankTableOffs = file->GetWordBE(infoBlockOffs + 0x0C);
  uint32_t bankTableCount = file->GetWordBE(infoBlockOffs + bankTableOffs);
  std::vector<Bank> bankTable;

  uint32_t bankTableIdx = infoBlockOffs + bankTableOffs + 0x08;
  for (uint32_t i = 0; i < bankTableCount; i++) {
    uint32_t bankBase = infoBlockOffs + file->GetWordBE(bankTableIdx);
    bankTableIdx += 0x08;

    Bank bank;
    uint32_t stringID = file->GetWordBE(bankBase + 0x00);
    if (stringID != 0xFFFFFFFF)
      bank.name = stringTable[stringID];
    bank.fileID = file->GetWordBE(bankBase + 0x04);
    bankTable.push_back(bank);
  }

  return bankTable;
}

std::vector<RSAR::File> RSAR::ReadFileTable(uint32_t infoBlockOffs) {
  uint32_t fileTableOffs = file->GetWordBE(infoBlockOffs + 0x1C);
  uint32_t fileTableCount = file->GetWordBE(infoBlockOffs + fileTableOffs);
  std::vector<File> fileTable;

  uint32_t fileTableIdx = infoBlockOffs + fileTableOffs + 0x08;
  for (uint32_t i = 0; i < fileTableCount; i++) {
    uint32_t fileBase = infoBlockOffs + file->GetWordBE(fileTableIdx);
    fileTableIdx += 0x08;

    File rsarFile = {};
    uint32_t filePosTableBase = infoBlockOffs + file->GetWordBE(fileBase + 0x18);
    uint32_t filePosTableCount = file->GetWordBE(filePosTableBase);

    /* Push a dummy file if we don't have anything and hope nothing references it... */
    if (filePosTableCount == 0) {
      fileTable.push_back(rsarFile);
      continue;
    }

    uint32_t filePosBase = infoBlockOffs + file->GetWordBE(filePosTableBase + 0x08);
    rsarFile.groupID = file->GetWordBE(filePosBase + 0x00);
    rsarFile.index = file->GetWordBE(filePosBase + 0x04);
    fileTable.push_back(rsarFile);
  }

  return fileTable;
}

std::vector<RSAR::Group> RSAR::ReadGroupTable(uint32_t infoBlockOffs) {
  uint32_t groupTableOffs = file->GetWordBE(infoBlockOffs + 0x24);
  uint32_t groupTableCount = file->GetWordBE(infoBlockOffs + groupTableOffs);
  std::vector<Group> groupTable;

  uint32_t groupTableIdx = infoBlockOffs + groupTableOffs + 0x08;
  for (uint32_t i = 0; i < groupTableCount; i++) {
    uint32_t groupBase = infoBlockOffs + file->GetWordBE(groupTableIdx);
    groupTableIdx += 0x08;

    uint32_t stringID = file->GetWordBE(groupBase + 0x00);
    Group group;
    if (stringID != 0xFFFFFFFF)
      group.name = stringTable[stringID];
    group.data.offset = file->GetWordBE(groupBase + 0x10);
    group.data.size = file->GetWordBE(groupBase + 0x14);
    group.waveData.offset = file->GetWordBE(groupBase + 0x18);
    group.waveData.size = file->GetWordBE(groupBase + 0x1C);

    uint32_t itemTableBase = infoBlockOffs + file->GetWordBE(groupBase + 0x24);
    uint32_t itemTableCount = file->GetWordBE(itemTableBase);
    uint32_t itemTableIdx = itemTableBase + 0x08;
    for (uint32_t j = 0; j < itemTableCount; j++) {
      uint32_t itemBase = infoBlockOffs + file->GetWordBE(itemTableIdx);
      itemTableIdx += 0x08;

      GroupItem item;
      item.fileID = file->GetWordBE(itemBase + 0x00);
      item.data.offset = file->GetWordBE(itemBase + 0x04);
      item.data.size = file->GetWordBE(itemBase + 0x08);
      item.waveData.offset = file->GetWordBE(itemBase + 0x0C);
      item.waveData.size = file->GetWordBE(itemBase + 0x10);
      group.items.push_back(item);
    }

    groupTable.push_back(group);
  }

  return groupTable;
}

RSAR::RBNK RSAR::ParseRBNK(Bank *bank) {
  File fileInfo = fileTable[bank->fileID];
  Group groupInfo = groupTable[fileInfo.groupID];
  GroupItem itemInfo = groupInfo.items[fileInfo.index];

  assert(itemInfo.fileID == bank->fileID);
  uint32_t rbnkOffset = itemInfo.data.offset + groupInfo.data.offset;
  uint32_t waveDataOffset = itemInfo.waveData.offset + groupInfo.waveData.offset;
  
  RBNK rbnk = {};
  rbnk.name = bank->name;

  if (!file->MatchBytes("RBNK\xFE\xFF\x01", rbnkOffset))
    return rbnk;

  uint32_t version = file->GetByte(rbnkOffset + 0x07);
  uint32_t dataBlockOffs = rbnkOffset + file->GetWordBE(rbnkOffset + 0x10);
  uint32_t waveBlockOffs = rbnkOffset + file->GetWordBE(rbnkOffset + 0x18);

  rbnk.instr = CheckBlock(dataBlockOffs, "DATA");
  rbnk.wave = CheckBlock(waveBlockOffs, "WAVE");
  rbnk.waveDataOffset = waveDataOffset;

  if (rbnk.wave.size != 0) {
    rbnk.type = RBNK::SampCollType::WAVE;
  } else {
    rbnk.type = RBNK::SampCollType::RWAR;
  }

  return rbnk;
}

RSAR::RSEQ RSAR::ParseRSEQ(Sound *sound) {
  assert(sound->type == Sound::SEQ);

  File fileInfo = fileTable[sound->fileID];
  Group groupInfo = groupTable[fileInfo.groupID];
  GroupItem itemInfo = groupInfo.items[fileInfo.index];

  uint32_t rseqOffset = itemInfo.data.offset + groupInfo.data.offset;

  RSEQ rseq = {};
  rseq.name = sound->name;

  if (!file->MatchBytes("RSEQ\xFE\xFF\x01", rseqOffset))
    return rseq;

  uint32_t version = file->GetByte(rseqOffset + 0x07);
  uint32_t dataBlockBase = rseqOffset + file->GetWordBE(rseqOffset + 0x10);

  FileRange dataBlock = CheckBlock(dataBlockBase, "DATA");
  rseq.rseqOffset = dataBlockBase + file->GetWordBE(dataBlock.offset + 0x00);
  rseq.dataOffset = sound->seq.dataOffset;
  rseq.bankIdx = sound->seq.bankID;
  return rseq;
}

bool RSAR::Parse() {
  if (!file->MatchBytes("RSAR\xFE\xFF\x01", 0))
    return false;

  uint32_t version = file->GetByte(0x07);
  uint32_t symbBlockOffs = file->GetWordBE(0x10);
  uint32_t infoBlockOffs = file->GetWordBE(0x18);
  uint32_t fileBlockOffs = file->GetWordBE(0x20);

  /*
  uint32_t playerTableOffs = file->GetWordBE(0x14);
  */

  FileRange symbBlockBase = CheckBlock(symbBlockOffs, "SYMB");
  stringTable = ParseSymbBlock(symbBlockBase.offset);

  FileRange infoBlockBase = CheckBlock(infoBlockOffs, "INFO");
  soundTable = ReadSoundTable(infoBlockBase.offset);
  bankTable = ReadBankTable(infoBlockBase.offset);
  fileTable = ReadFileTable(infoBlockBase.offset);
  groupTable = ReadGroupTable(infoBlockBase.offset);

  for (size_t i = 0; i < bankTable.size(); i++)
    rbnks.push_back(ParseRBNK(&bankTable[i]));

  for (size_t i = 0; i < soundTable.size(); i++)
    rseqs.push_back(ParseRSEQ(&soundTable[i]));

  return true;
}

VGMSampColl * RSARScanner::LoadBankSampColl(RawFile *file, RSAR::RBNK *rbnk) {
  switch (rbnk->type) {
  case RSAR::RBNK::SampCollType::RWAR:
    return new RSARSampCollRWAR(file, rbnk->waveDataOffset, 0, string2wstring(rbnk->name));
  case RSAR::RBNK::SampCollType::WAVE:
    return new RSARSampCollWAVE(file, rbnk->wave.offset, rbnk->wave.size, rbnk->waveDataOffset, 0, string2wstring(rbnk->name));
  default:
    return nullptr;
  }
}

void RSARScanner::Scan(RawFile *file, void *info) {
  RSAR rsar(file);
 
  if (!rsar.Parse())
    return;

  /* Load all the banks. */
  std::vector<VGMSampColl *> sampColls;
  std::vector<VGMInstrSet *> instrSets;
  for (size_t i = 0; i < rsar.rbnks.size(); i++) {
    RSAR::RBNK *rbnk = &rsar.rbnks[i];
    VGMSampColl *sampColl = LoadBankSampColl(file, rbnk);
    sampColl->LoadVGMFile();
    sampColls.push_back(sampColl);

    VGMInstrSet *instrSet = new RSARInstrSet(file, rbnk->instr.offset, rbnk->instr.size, string2wstring(rbnk->name));
    instrSet->LoadVGMFile();
    instrSets.push_back(instrSet);
  }

  for (size_t i = 0; i < rsar.rseqs.size(); i++) {
    RSAR::RSEQ *rseq = &rsar.rseqs[i];
    VGMSeq *seq = new RSARSeq(file, rseq->rseqOffset, rseq->dataOffset, 0, string2wstring(rseq->name));
    seq->LoadVGMFile();

    VGMColl *coll = new VGMColl(string2wstring(rseq->name));
    coll->AddSampColl(sampColls[rseq->bankIdx]);
    coll->AddInstrSet(instrSets[rseq->bankIdx]);
    coll->UseSeq(seq);

    coll->Load();
  }
}
