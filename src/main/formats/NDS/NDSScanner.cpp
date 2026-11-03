/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NDSScanner.h"

#include "base/Types.h"
#include "components/VGMMetadataHint.h"
#include "NDSInstrSet.h"
#include "NDSSeq.h"
#include "ScannerManager.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

namespace vgmtrans::scanners {
  ScannerRegistration<NDSScanner> s_nds("NDS", {"nds", "sdat", "mini2sf", "2sf", "2sflib"});
}

namespace {

/* Observed from multiple samples, the maximum length of standard archives is 127 + null terminator */
constexpr auto MAX_NAME_LEN = 128;
constexpr auto NDS_FORMAT_NAME = "NDS";
constexpr std::string_view SSEQ_SIGNATURE{"SSEQ\xFF\xFE\x00\x01", 8};
constexpr u32 MAX_PADDING_BEFORE_SSEQ = 0x200;

bool rawFileHasBytes(RawFile* file, u32 offset, u32 size) {
  return offset <= file->size() && size <= file->size() - offset;
}

bool matchesBytes(RawFile* file, u32 offset, std::string_view signature) {
  if (!rawFileHasBytes(file, offset, static_cast<u32>(signature.size()))) {
    return false;
  }
  for (size_t i = 0; i < signature.size(); ++i) {
    if (file->readByte(offset + i) != static_cast<u8>(signature[i])) {
      return false;
    }
  }
  return true;
}

bool isZeroFilled(RawFile* file, u32 begin, u32 end) {
  for (u32 offset = begin; offset < end && offset < file->size(); ++offset) {
    if (file->readByte(offset) != 0) {
      return false;
    }
  }
  return true;
}

std::optional<u32> nearbySseqHeader(RawFile* file, u32 offset, u32 size) {
  const u32 searchEnd =
      static_cast<u32>(std::min<u64>(file->size(), static_cast<u64>(offset) + size + MAX_PADDING_BEFORE_SSEQ));
  for (u32 candidate = offset + 1; candidate + SSEQ_SIGNATURE.size() <= searchEnd; ++candidate) {
    if (matchesBytes(file, candidate, SSEQ_SIGNATURE)) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::optional<u32> recoveredSseqOffset(RawFile* file, u32 offset, u32 size) {
  const auto sseqOffset = nearbySseqHeader(file, offset, size);
  if (!sseqOffset) {
    return std::nullopt;
  }

  const u32 trackStart = offset + 0x1c;
  const u32 paddingEnd = std::min(*sseqOffset, offset + size);
  if (size <= 0x100 && *sseqOffset >= trackStart && isZeroFilled(file, offset, paddingEnd) &&
      ((*sseqOffset - trackStart) % 3) == 2) {
    return std::nullopt;
  }
  return sseqOffset;
}

const VGMMetadataHint* findNDSMetadataHint(RawFile* file,
                                           u32 seqIndex,
                                           const std::string& seqName,
                                           u32 seqOffset) {
  if (!seqName.empty()) {
    if (const auto* hint = file->findMetadataHint(VGMMetadataHintQuery{
        .targetFormat = NDS_FORMAT_NAME,
        .lookupKey = seqName,
    })) {
      return hint;
    }
  }

  if (const auto* hint = file->findMetadataHint(VGMMetadataHintQuery{
      .targetFormat = NDS_FORMAT_NAME,
      .songIndex = seqIndex,
  })) {
    return hint;
  }

  return file->findMetadataHint(VGMMetadataHintQuery{
      .targetFormat = NDS_FORMAT_NAME,
      .fileOffset = seqOffset,
  });
}

std::string displayNameForNDSSeq(RawFile* file,
                                 u32 seqIndex,
                                 const std::string& seqName,
                                 u32 seqOffset) {
  const auto* hint = findNDSMetadataHint(file, seqIndex, seqName, seqOffset);
  if (hint && hint->tag.hasTitle()) {
    return hint->tag.title;
  }

  return seqName;
}

}  // namespace

void NDSScanner::scan(RawFile* file, void* /*info*/) {
  searchForSDAT(file);
}

void NDSScanner::searchForSDAT(RawFile *file) {
  using namespace std::string_literals;
  const std::string signature = "SDAT\xFF\xFE\x00\x01"s;

  auto it = std::search(file->begin(), file->end(),
    std::boyer_moore_searcher(signature.begin(), signature.end()));
  while (it != file->end()) {
    size_t offset = it - file->begin();
    if (file->get<u32>(offset + 0x10) < 0x10000) {
      loadFromSDAT(file, offset);
    }

    it = std::search(std::next(it), file->end(),
      std::boyer_moore_searcher(signature.begin(), signature.end()));
  }
}

// The following is pretty god-awful messy.  I should have created structs for the different
// blocks and loading the entire blocks at a time.  
u32 NDSScanner::loadFromSDAT(RawFile *file, u32 baseOff) {
  u32 SYMBoff;
  u32 INFOoff;
  u32 FAToff;
  u32 nSeqs;
  u32 nBnks;
  u32 nWAs;
  std::vector<std::string> seqNames;
  std::vector<std::string> bnkNames;
  std::vector<std::string> waNames;
  std::vector<u16> seqFileIDs;
  std::vector<u16> bnkFileIDs;
  std::vector<u16> waFileIDs;
  std::vector<u16> seqFileBnks;
  std::vector<std::vector<u16> > bnkWAs;
  std::vector<NDSWaveArch *> WAs;
  std::vector<std::pair<u16, NDSInstrSet *> > BNKs;

  u32 SDATLength = file->readWord(baseOff + 8) + 8;

  SYMBoff = file->readWord(baseOff + 0x10) + baseOff;
  INFOoff = file->readWord(baseOff + 0x18) + baseOff;
  FAToff = file->readWord(baseOff + 0x20) + baseOff;
  bool hasSYMB = (SYMBoff != baseOff);

  nSeqs = file->readWord(file->readWord(INFOoff + 0x08) + INFOoff);
  nBnks = file->readWord(file->readWord(INFOoff + 0x10) + INFOoff);
  nWAs = file->readWord(file->readWord(INFOoff + 0x14) + INFOoff);

  u32 pSeqNamePtrList = 0;
  u32 pBnkNamePtrList = 0;
  u32 pWANamePtrList = 0;
  if (hasSYMB) {
    pSeqNamePtrList = file->readWord(SYMBoff + 0x08) + SYMBoff;        //get pointer to list of sequence name pointers
    pBnkNamePtrList = file->readWord(SYMBoff + 0x10) + SYMBoff;        //get pointer to list of bank name pointers
    pWANamePtrList = file->readWord(SYMBoff + 0x14) + SYMBoff;        //get pointer to list of wavearchive name pointers
  }

  for (u32 i = 0; i < nSeqs; i++) {
    if (hasSYMB) {
      seqNames.push_back(file->readNullTerminatedString(file->readWord(pSeqNamePtrList + 4 + i * 4) + SYMBoff, MAX_NAME_LEN));
    }
    else {
      seqNames.push_back(fmt::format("SSEQ_{:04d}", i));
    }
  }

  for (u32 i = 0; i < nBnks; i++) {
    if (hasSYMB) {
      bnkNames.push_back(file->readNullTerminatedString(file->readWord(pBnkNamePtrList + 4 + i * 4) + SYMBoff, MAX_NAME_LEN));
    }
    else {
      bnkNames.push_back(fmt::format("SBNK_{:04d}", i));
    }
  }

  for (u32 i = 0; i < nWAs; i++) {
    if (hasSYMB) {
      waNames.push_back(file->readNullTerminatedString(file->readWord(pWANamePtrList + 4 + i * 4) + SYMBoff, MAX_NAME_LEN));
    }
    else {
      waNames.push_back(fmt::format("SWAR_{:04d}", i));
    }
  }

  u32 pSeqInfoPtrList = file->readWord(INFOoff + 8) + INFOoff;
  //u32 seqInfoPtrListLength = file->GetWord(INFOoff + 12);
  u32 nSeqInfos = file->readWord(pSeqInfoPtrList);
  for (u32 i = 0; i < nSeqInfos; i++) {
    u32 pSeqInfoUnadjusted = file->readWord(pSeqInfoPtrList + 4 + i * 4);
    u32 pSeqInfo = INFOoff + pSeqInfoUnadjusted;
    if (pSeqInfoUnadjusted == 0)
      seqFileIDs.push_back((u16) -1);
    else
      seqFileIDs.push_back(file->readShort(pSeqInfo));
    seqFileBnks.push_back(file->readShort(pSeqInfo + 4));
    //next bytes would be vol, cpr, ppr, and ply respectively, whatever the heck those last 3 stand for
  }

  u32 pBnkInfoPtrList = file->readWord(INFOoff + 0x10) + INFOoff;
  u32 nBnkInfos = file->readWord(pBnkInfoPtrList);
  for (u32 i = 0; i < nBnkInfos; i++) {
    u32 pBnkInfoUnadjusted = file->readWord(pBnkInfoPtrList + 4 + i * 4);
    u32 pBnkInfo = INFOoff + pBnkInfoUnadjusted;
    if (pBnkInfoUnadjusted == 0)
      bnkFileIDs.push_back((u16) -1);
    else
      bnkFileIDs.push_back(file->readShort(pBnkInfo));
    bnkWAs.push_back(std::vector<u16>());
    std::vector<std::vector<u16> >::reference ref = bnkWAs.back();
    for (int j = 0; j < 4; j++) {
      u16 WANum = file->readShort(pBnkInfo + 4 + (j * 2));
      //if (WANum > 0x200)			//insanity check
      if (WANum >= nWAs)
        ref.push_back(0xFFFF);
      else
        ref.push_back(WANum);
    }
  }

  u32 pWAInfoList = file->readWord(INFOoff + 0x14) + INFOoff;
  u32 nWAInfos = file->readWord(pWAInfoList);
  for (u32 i = 0; i < nWAInfos; i++) {
    u32 pWAInfoUnadjusted = file->readWord(pWAInfoList + 4 + i * 4);
    u32 pWAInfo = INFOoff + pWAInfoUnadjusted;
    if (pWAInfoUnadjusted == 0)
      waFileIDs.push_back((u16) -1);
    else
      waFileIDs.push_back(file->readShort(pWAInfo));
  }

  auto* psg_sampcoll = pRoot->loadVGMFile<NDSPSG>(file);
  if (!psg_sampcoll) {
    return SDATLength;
  }

  {
    std::vector<u16> vUniqueWAs;// = vector<u16>(bnkWAs);
    for (u32 i = 0; i < bnkWAs.size(); i++)
      vUniqueWAs.insert(vUniqueWAs.end(), bnkWAs[i].begin(), bnkWAs[i].end());
    sort(vUniqueWAs.begin(), vUniqueWAs.end());
    std::vector<u16>::iterator new_end = unique(vUniqueWAs.begin(), vUniqueWAs.end());

    std::vector<bool> valid;
    valid.resize(nWAs);
    for (std::vector<u16>::iterator iter = vUniqueWAs.begin(); iter != new_end; iter++) {
      if ((*iter != (u16) -1) && (*iter < valid.size()))
        valid[*iter] = 1;
    }

    for (u32 i = 0; i < nWAs; i++)
      //for (vector<u16>::iterator iter = vUniqueWAs.begin(); iter != new_end; iter++)
    {
      if (valid[i] != 1 || waFileIDs[i] == (u16) -1) {
        WAs.push_back(NULL);
        continue;
      }
      u32 offset = FAToff + 12 + waFileIDs[i] * 0x10;
      u32 pWAFatData = file->readWord(offset) + baseOff;
      offset += 4;
      u32 fileSize = file->readWord(offset);
      auto* NewNDSwa = pRoot->loadVGMFile<NDSWaveArch>(file, pWAFatData, fileSize, waNames[i]);
      if (!NewNDSwa) {
        L_ERROR("Failed to load NDSWaveArch at 0x{:08X}", pWAFatData);
        WAs.push_back(NULL);
        continue;
      }
      WAs.push_back(NewNDSwa);
    }
  }

  {
    std::vector<u16> vUniqueBanks = std::vector<u16>(seqFileBnks);
    sort(vUniqueBanks.begin(), vUniqueBanks.end());
    std::vector<u16>::iterator new_end = unique(vUniqueBanks.begin(), vUniqueBanks.end());

    //for (u32 i=0; i<nBnks; i++)
    //for (u32 i=0; i<seqFileBnks.size(); i++)
    for (std::vector<u16>::iterator iter = vUniqueBanks.begin(); iter != new_end; iter++) {
      if (*iter >= bnkFileIDs.size() /*0x1000*/|| bnkFileIDs[*iter]
          == (u16) -1)    // > 0x1000 is idiot test for Phoenix Wright, which had many 0x1C80 values, as if they were 0xFFFF
        continue;
      u32 offset = FAToff + 12 + bnkFileIDs[*iter] * 0x10;
      u32 pBnkFatData = file->readWord(offset) + baseOff;
      offset += 4;
      u32 fileSize = file->readWord(offset);
      //if (bnkWAs[*iter][0] == (u16)-1 || numWAs != 1)
      //	continue;
      auto NewNDSInstrSet = std::make_unique<NDSInstrSet>(file, pBnkFatData, fileSize, psg_sampcoll,
                                                          bnkNames[*iter]);
      for (int i = 0; i < 4; i++)        //use first WA found.  Ideally, should load all WAs
      {
        short WAnum = bnkWAs[*iter][i];
        if (WAnum != -1)
          NewNDSInstrSet->addWaveArchSampColl(WAs[WAnum]);
        else
          NewNDSInstrSet->addWaveArchSampColl(nullptr);
      }
      auto* rawInstrSet = NewNDSInstrSet.get();
      if (!pRoot->loadVGMFile(std::move(NewNDSInstrSet))) {
        L_ERROR("Failed to load NDSInstrSet at 0x{:08X}", pBnkFatData);
        continue;
      }
      std::pair<u16, NDSInstrSet *> theBank(*iter, rawInstrSet);
      BNKs.push_back(theBank);
    }
  }

  {
    //vector<u16> vUniqueSeqs = vector<u16>(seqFileIDs);
    //sort(vUniqueSeqs.begin(), vUniqueSeqs.end());
    //vector<u16>::iterator new_end = unique(vUniqueBanks.begin(), vUniqueBanks.end());

    for (u32 i = 0; i < nSeqs; i++) {
      if (seqFileIDs[i] == (u16) -1)
        continue;
      u32 offset = FAToff + 12 + seqFileIDs[i] * 0x10;
      u32 pSeqFatData = file->readWord(offset) + baseOff;
      offset += 4;
      u32 fileSize = file->readWord(offset);
      // Some 2SF SDATs have FAT entries that point at padding just before the
      // real SSEQ. Decode the real header instead of turning padding into notes.
      if (!matchesBytes(file, pSeqFatData, SSEQ_SIGNATURE)) {
        if (const auto recovered = recoveredSseqOffset(file, pSeqFatData, fileSize)) {
          pSeqFatData = *recovered;
          if (rawFileHasBytes(file, pSeqFatData + 8, 4)) {
            fileSize = file->readWord(pSeqFatData + 8);
          }
        }
      }
      const auto displayName = displayNameForNDSSeq(file, i, seqNames[i], pSeqFatData);
      auto* NewNDSSeq = pRoot->loadVGMFile<NDSSeq>(file, pSeqFatData, fileSize, displayName);
      if (!NewNDSSeq) {
        L_ERROR("Failed to load NDSSeq at 0x{:08X}", pSeqFatData);
        continue;
      }

      auto coll = std::make_unique<VGMColl>(displayName);
      coll->attachSeq(NewNDSSeq);
      u32 bnkIndex = 0;
      for (u32 j = 0; j < BNKs.size(); j++) {
        if (seqFileBnks[i] == BNKs[j].first) {
          bnkIndex = j;
          break;
        }
      }
      
      coll->attachSampColl(psg_sampcoll);
      coll->attachInstrSet(BNKs[bnkIndex].second);
      for (int j = 0; j < 4; j++) {
        short WAnum = bnkWAs[seqFileBnks[i]][j];
        if (WAnum != -1)
          coll->attachSampColl(WAs[WAnum]);
      }
      pRoot->loadVGMColl(std::move(coll));
    }
  }
  return SDATLength;
}
