#include <string>
#include "pch.h"
#include "common.h"
#include "NDSScanner.h"
#include "NDSSeq.h"
#include "NDSInstrSet.h"

using namespace std;

#define SRCH_BUF_SIZE 0x20000

void NDSScanner::Scan(RawFile *file, void *info) {
  SearchForSDAT(file);
  return;
}

void NDSScanner::SearchForSDAT(RawFile *file) {
  uint32_t nFileLength = file->size();
  for (uint32_t i = 0; i + 4 < nFileLength; i++) {
    if ((*file)[i] == 'S' && (*file)[i + 1] == 'D' && (*file)[i + 2] == 'A' && (*file)[i + 3] == 'T'
        && (*file)[i + 4] == 0xFF && (*file)[i + 5] == 0xFE && (*file)[i + 6] == 0
        && (*file)[i + 7] == 0x01 && (file->GetShort(i + 12) < 0x100)
        && (file->GetWord(i + 0x10) < 0x10000)) {
      i += LoadFromSDAT(file, i);
    }
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
  vector<string> seqNames;
  vector<string> bnkNames;
  vector<string> waNames;
  vector<uint16_t> seqFileIDs;
  vector<uint16_t> bnkFileIDs;
  vector<uint16_t> waFileIDs;
  vector<uint16_t> seqFileBnks;
  vector<vector<uint16_t> > bnkWAs;
  vector<NDSWaveArch *> WAs;
  vector<pair<uint16_t, NDSInstrSet *> > BNKs;

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
    bnkWAs.push_back(vector<uint16_t>());
    vector<vector<uint16_t> >::reference ref = bnkWAs.back();
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
    vector<uint16_t> vUniqueWAs;// = vector<uint16_t>(bnkWAs);
    for (uint32_t i = 0; i < bnkWAs.size(); i++)
      vUniqueWAs.insert(vUniqueWAs.end(), bnkWAs[i].begin(), bnkWAs[i].end());
    sort(vUniqueWAs.begin(), vUniqueWAs.end());
    vector<uint16_t>::iterator new_end = unique(vUniqueWAs.begin(), vUniqueWAs.end());

    vector<bool> valid;
    valid.resize(nWAs);
    for (vector<uint16_t>::iterator iter = vUniqueWAs.begin(); iter != new_end; iter++) {
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
        pRoot->AddLogItem(new LogItem(FormatString<string>("Failed to load NDSWaveArch at 0x%08x\n",
                                                            pWAFatData).c_str(), LOG_LEVEL_ERR, "NDSScanner"));
        WAs.push_back(NULL);
        delete NewNDSwa;
        continue;
      }
      WAs.push_back(NewNDSwa);
    }
  }

  {
    vector<uint16_t> vUniqueBanks = vector<uint16_t>(seqFileBnks);
    sort(vUniqueBanks.begin(), vUniqueBanks.end());
    vector<uint16_t>::iterator new_end = unique(vUniqueBanks.begin(), vUniqueBanks.end());

    //for (uint32_t i=0; i<nBnks; i++)
    //for (uint32_t i=0; i<seqFileBnks.size(); i++)
    for (vector<uint16_t>::iterator iter = vUniqueBanks.begin(); iter != new_end; iter++) {
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
        pRoot->AddLogItem(new LogItem(("Failed to load NDSInstrSet at " + std::to_string(
                                                            pBnkFatData)).c_str(), LOG_LEVEL_ERR, "NDSScanner"));
      }
      pair<uint16_t, NDSInstrSet *> theBank(*iter, NewNDSInstrSet);
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
        pRoot->AddLogItem(new LogItem(FormatString<string>("Failed to load NDSSeq at 0x%08x\n", pSeqFatData).c_str(),
                                      LOG_LEVEL_ERR,
                                      "NDSScanner"));
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
