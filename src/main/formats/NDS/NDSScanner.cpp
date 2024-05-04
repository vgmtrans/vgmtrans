/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NDSSeq.h"
#include "NDSInstrSet.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<NDSScanner> s_nds("NDS", {"nds", "sdat", "mini2sf", "2sf", "2sflib"});
}

void NDSScanner::Scan(RawFile *file, void *info) {
  SearchForSDAT(file);
}

void NDSScanner::SearchForSDAT(RawFile *file) {
  using namespace std::string_literals;
  const std::string signature = "SDAT\xFF\xFE\x00\x01"s;

  auto it = std::search(file->begin(), file->end(),
    std::boyer_moore_searcher(signature.begin(), signature.end()));
  while (it != file->end()) {
    int offset = it - file->begin();
    if (file->get<u32>(offset + 0x10) < 0x10000) {
      LoadFromSDAT(file, offset);
    }

    it = std::search(std::next(it), file->end(),
      std::boyer_moore_searcher(signature.begin(), signature.end()));
  }
}

// The following is pretty god-awful messy.  I should have created structs for the different
// blocks and loading the entire blocks at a time.  
uint32_t NDSScanner::LoadFromSDAT(RawFile *file, uint32_t baseOff) {
  uint32_t SYMBoff;
  uint32_t INFOoff;
  uint32_t FAToff;
  uint32_t nSeqs;
  uint32_t nBnks;
  uint32_t nWAs;
  std::vector<std::string> seqNames;
  std::vector<std::string> bnkNames;
  std::vector<std::string> waNames;
  std::vector<uint16_t> seqFileIDs;
  std::vector<uint16_t> bnkFileIDs;
  std::vector<uint16_t> waFileIDs;
  std::vector<uint16_t> seqFileBnks;
  std::vector<std::vector<uint16_t> > bnkWAs;
  std::vector<NDSWaveArch *> WAs;
  std::vector<std::pair<uint16_t, NDSInstrSet *> > BNKs;

  uint32_t SDATLength = file->GetWord(baseOff + 8) + 8;

  SYMBoff = file->GetWord(baseOff + 0x10) + baseOff;
  INFOoff = file->GetWord(baseOff + 0x18) + baseOff;
  FAToff = file->GetWord(baseOff + 0x20) + baseOff;
  bool hasSYMB = (SYMBoff != baseOff);

  nSeqs = file->GetWord(file->GetWord(INFOoff + 0x08) + INFOoff);
  nBnks = file->GetWord(file->GetWord(INFOoff + 0x10) + INFOoff);
  nWAs = file->GetWord(file->GetWord(INFOoff + 0x14) + INFOoff);

  uint32_t pSeqNamePtrList;
  uint32_t pBnkNamePtrList;
  uint32_t pWANamePtrList;
  if (hasSYMB) {
    pSeqNamePtrList = file->GetWord(SYMBoff + 0x08) + SYMBoff;        //get pointer to list of sequence name pointers
    pBnkNamePtrList = file->GetWord(SYMBoff + 0x10) + SYMBoff;        //get pointer to list of bank name pointers
    pWANamePtrList = file->GetWord(SYMBoff + 0x14) + SYMBoff;        //get pointer to list of wavearchive name pointers
  }

  for (uint32_t i = 0; i < nSeqs; i++) {
    char temp[32];        //that 32 is totally arbitrary, i should change it
    if (hasSYMB) {
      file->GetBytes(file->GetWord(pSeqNamePtrList + 4 + i * 4) + SYMBoff, 32, temp);
    }
    else {
      snprintf(temp, 32, "SSEQ_%04d", i);
    }
    seqNames.push_back(temp);
  }

  for (uint32_t i = 0; i < nBnks; i++) {
    char temp[32];        //that 32 is totally arbitrary, i should change it

    if (hasSYMB) {
      file->GetBytes(file->GetWord(pBnkNamePtrList + 4 + i * 4) + SYMBoff, 32, temp);
    }
    else {
      snprintf(temp, 32, "SBNK_%04d", i);
    }
    bnkNames.push_back(temp);
  }

  for (uint32_t i = 0; i < nWAs; i++) {
    char temp[32];        //that 32 is totally arbitrary, i should change it

    if (hasSYMB) {
      file->GetBytes(file->GetWord(pWANamePtrList + 4 + i * 4) + SYMBoff, 32, temp);
    }
    else {
      snprintf(temp, 32, "SWAR_%04d", i);
    }
    waNames.push_back(temp);
  }

  uint32_t pSeqInfoPtrList = file->GetWord(INFOoff + 8) + INFOoff;
  //uint32_t seqInfoPtrListLength = file->GetWord(INFOoff + 12);
  uint32_t nSeqInfos = file->GetWord(pSeqInfoPtrList);
  for (uint32_t i = 0; i < nSeqInfos; i++) {
    uint32_t pSeqInfoUnadjusted = file->GetWord(pSeqInfoPtrList + 4 + i * 4);
    uint32_t pSeqInfo = INFOoff + pSeqInfoUnadjusted;
    if (pSeqInfoUnadjusted == 0)
      seqFileIDs.push_back((uint16_t) -1);
    else
      seqFileIDs.push_back(file->GetShort(pSeqInfo));
    seqFileBnks.push_back(file->GetShort(pSeqInfo + 4));
    //next bytes would be vol, cpr, ppr, and ply respectively, whatever the heck those last 3 stand for
  }

  uint32_t pBnkInfoPtrList = file->GetWord(INFOoff + 0x10) + INFOoff;
  uint32_t nBnkInfos = file->GetWord(pBnkInfoPtrList);
  for (uint32_t i = 0; i < nBnkInfos; i++) {
    uint32_t pBnkInfoUnadjusted = file->GetWord(pBnkInfoPtrList + 4 + i * 4);
    uint32_t pBnkInfo = INFOoff + pBnkInfoUnadjusted;
    if (pBnkInfoUnadjusted == 0)
      bnkFileIDs.push_back((uint16_t) -1);
    else
      bnkFileIDs.push_back(file->GetShort(pBnkInfo));
    bnkWAs.push_back(std::vector<uint16_t>());
    std::vector<std::vector<uint16_t> >::reference ref = bnkWAs.back();
    for (int j = 0; j < 4; j++) {
      uint16_t WANum = file->GetShort(pBnkInfo + 4 + (j * 2));
      //if (WANum > 0x200)			//insanity check
      if (WANum >= nWAs)
        ref.push_back(0xFFFF);
      else
        ref.push_back(WANum);
    }
  }

  uint32_t pWAInfoList = file->GetWord(INFOoff + 0x14) + INFOoff;
  uint32_t nWAInfos = file->GetWord(pWAInfoList);
  for (uint32_t i = 0; i < nWAInfos; i++) {
    uint32_t pWAInfoUnadjusted = file->GetWord(pWAInfoList + 4 + i * 4);
    uint32_t pWAInfo = INFOoff + pWAInfoUnadjusted;
    if (pWAInfoUnadjusted == 0)
      waFileIDs.push_back((uint16_t) -1);
    else
      waFileIDs.push_back(file->GetShort(pWAInfo));
  }

  NDSPSG* psg_sampcoll = new NDSPSG(file);
  psg_sampcoll->LoadVGMFile();

  {
    std::vector<uint16_t> vUniqueWAs;// = vector<uint16_t>(bnkWAs);
    for (uint32_t i = 0; i < bnkWAs.size(); i++)
      vUniqueWAs.insert(vUniqueWAs.end(), bnkWAs[i].begin(), bnkWAs[i].end());
    sort(vUniqueWAs.begin(), vUniqueWAs.end());
    std::vector<uint16_t>::iterator new_end = unique(vUniqueWAs.begin(), vUniqueWAs.end());

    std::vector<bool> valid;
    valid.resize(nWAs);
    for (std::vector<uint16_t>::iterator iter = vUniqueWAs.begin(); iter != new_end; iter++) {
      if ((*iter != (uint16_t) -1) && (*iter < valid.size()))
        valid[*iter] = 1;
    }

    for (uint32_t i = 0; i < nWAs; i++)
      //for (vector<uint16_t>::iterator iter = vUniqueWAs.begin(); iter != new_end; iter++)
    {
      if (valid[i] != 1 || waFileIDs[i] == (uint16_t) -1) {
        WAs.push_back(NULL);
        continue;
      }
      uint32_t offset = FAToff + 12 + waFileIDs[i] * 0x10;
      uint32_t pWAFatData = file->GetWord(offset) + baseOff;
      offset += 4;
      uint32_t fileSize = file->GetWord(offset);
      NDSWaveArch *NewNDSwa = new NDSWaveArch(file, pWAFatData, fileSize, waNames[i]);
      if (!NewNDSwa->LoadVGMFile()) {
        L_ERROR("Failed to load NDSWaveArch at 0x{:08X}", pWAFatData);
        WAs.push_back(NULL);
        delete NewNDSwa;
        continue;
      }
      WAs.push_back(NewNDSwa);
    }
  }

  {
    std::vector<uint16_t> vUniqueBanks = std::vector<uint16_t>(seqFileBnks);
    sort(vUniqueBanks.begin(), vUniqueBanks.end());
    std::vector<uint16_t>::iterator new_end = unique(vUniqueBanks.begin(), vUniqueBanks.end());

    //for (uint32_t i=0; i<nBnks; i++)
    //for (uint32_t i=0; i<seqFileBnks.size(); i++)
    for (std::vector<uint16_t>::iterator iter = vUniqueBanks.begin(); iter != new_end; iter++) {
      if (*iter >= bnkFileIDs.size() /*0x1000*/|| bnkFileIDs[*iter]
          == (uint16_t) -1)    // > 0x1000 is idiot test for Phoenix Wright, which had many 0x1C80 values, as if they were 0xFFFF
        continue;
      uint32_t offset = FAToff + 12 + bnkFileIDs[*iter] * 0x10;
      uint32_t pBnkFatData = file->GetWord(offset) + baseOff;
      offset += 4;
      uint32_t fileSize = file->GetWord(offset);
      //if (bnkWAs[*iter][0] == (uint16_t)-1 || numWAs != 1)
      //	continue;
      NDSInstrSet *NewNDSInstrSet = new NDSInstrSet(file, pBnkFatData, fileSize, psg_sampcoll,
                                                    bnkNames[*iter]);
      for (int i = 0; i < 4; i++)        //use first WA found.  Ideally, should load all WAs
      {
        short WAnum = bnkWAs[*iter][i];
        if (WAnum != -1)
          NewNDSInstrSet->sampCollWAList.push_back(WAs[WAnum]);
        else
          NewNDSInstrSet->sampCollWAList.push_back(NULL);
      }
      if (!NewNDSInstrSet->LoadVGMFile()) {
        L_ERROR("Failed to load NDSInstrSet at 0x{:08X}", pBnkFatData);
      }
      std::pair<uint16_t, NDSInstrSet *> theBank(*iter, NewNDSInstrSet);
      BNKs.push_back(theBank);
    }
  }

  {
    //vector<uint16_t> vUniqueSeqs = vector<uint16_t>(seqFileIDs);
    //sort(vUniqueSeqs.begin(), vUniqueSeqs.end());
    //vector<uint16_t>::iterator new_end = unique(vUniqueBanks.begin(), vUniqueBanks.end());

    for (uint32_t i = 0; i < nSeqs; i++) {
      if (seqFileIDs[i] == (uint16_t) -1)
        continue;
      uint32_t offset = FAToff + 12 + seqFileIDs[i] * 0x10;
      uint32_t pSeqFatData = file->GetWord(offset) + baseOff;
      offset += 4;
      uint32_t fileSize = file->GetWord(offset);
      NDSSeq *NewNDSSeq = new NDSSeq(file, pSeqFatData, fileSize, seqNames[i]);
      if (!NewNDSSeq->LoadVGMFile()) {
        L_ERROR("Failed to load NDSSeq at 0x{:08X}", pSeqFatData);
      }

      VGMColl *coll = new VGMColl(seqNames[i]);
      coll->UseSeq(NewNDSSeq);
      uint32_t bnkIndex = 0;
      for (uint32_t j = 0; j < BNKs.size(); j++) {
        if (seqFileBnks[i] == BNKs[j].first) {
          bnkIndex = j;
          break;
        }
      }
      
      NDSInstrSet *instrset = BNKs[bnkIndex].second;
      coll->AddSampColl(psg_sampcoll);
      coll->AddInstrSet(BNKs[bnkIndex].second);
      for (int j = 0; j < 4; j++) {
        short WAnum = bnkWAs[seqFileBnks[i]][j];
        if (WAnum != -1)
          coll->AddSampColl(WAs[WAnum]);
      }
      if (!coll->Load()) {
        delete coll;
      }
    }
  }
  return SDATLength;
}
