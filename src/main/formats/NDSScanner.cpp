/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NDSSeq.h"
#include "NDSInstrSet.h"

#include <fmt/format.h>
#include <functional>
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<NDSScanner> s_nds("NDS", {"nds", "sdat", "mini2sf", "2sf", "2sflib"});
}

void NDSScanner::Scan(RawFile *file, void *) {
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
    uint32_t SDATLength = file->GetWord(baseOff + 8) + 8;

    auto SYMBoff = file->GetWord(baseOff + 0x10) + baseOff;
    auto INFOoff = file->GetWord(baseOff + 0x18) + baseOff;
    auto FAToff = file->GetWord(baseOff + 0x20) + baseOff;
    bool hasSYMB = (SYMBoff != baseOff);

    auto nSeqs = file->GetWord(file->GetWord(INFOoff + 0x08) + INFOoff);
    auto nBnks = file->GetWord(file->GetWord(INFOoff + 0x10) + INFOoff);
    auto nWAs = file->GetWord(file->GetWord(INFOoff + 0x14) + INFOoff);

    std::vector<std::string> bnkNames(nBnks), seqNames(nSeqs), waNames(nWAs);
    uint32_t pSeqNamePtrList = 0, pBnkNamePtrList = 0, pWANamePtrList = 0;
    if (hasSYMB) {
        /* Pointer to the list of sequence name pointers */
        pSeqNamePtrList = file->GetWord(SYMBoff + 0x08) + SYMBoff;
        /* Pointer to the list of bank name pointers */
        pBnkNamePtrList = file->GetWord(SYMBoff + 0x10) + SYMBoff;
        /* Pointer to the list of wave archive name pointers */
        pWANamePtrList = file->GetWord(SYMBoff + 0x14) + SYMBoff;

        char temp[32] = {0};
        for (int i = 0; i < nBnks; i++) {
            file->GetBytes(file->GetWord(pBnkNamePtrList + 4 + i * 4) + SYMBoff, 32, temp);
            bnkNames[i] = temp;
        }

        for (int i = 0; i < nSeqs; i++) {
            file->GetBytes(file->GetWord(pSeqNamePtrList + 4 + i * 4) + SYMBoff, 32, temp);
            seqNames[i] = temp;
        }

        for (int i = 0; i < nWAs; i++) {
            file->GetBytes(file->GetWord(pWANamePtrList + 4 + i * 4) + SYMBoff, 32, temp);
            waNames[i] = temp;
        }
    } else {
        std::generate(std::begin(bnkNames), std::end(bnkNames),
                      [n = 0]() mutable { return fmt::format("SBNK_{}", n++); });
        std::generate(std::begin(seqNames), std::end(seqNames),
                      [n = 0]() mutable { return fmt::format("SSEQ_{}", n++); });
        std::generate(std::begin(waNames), std::end(waNames),
                      [n = 0]() mutable { return fmt::format("SWAR_{}", n++); });
    }

    uint32_t pSeqInfoPtrList = file->GetWord(INFOoff + 8) + INFOoff;
    // uint32_t seqInfoPtrListLength = file->GetWord(INFOoff + 12);
    uint32_t nSeqInfos = file->GetWord(pSeqInfoPtrList);
    std::vector<uint16_t> seqFileIDs(nSeqInfos), seqFileBnks(nSeqInfos);
    for (uint32_t i = 0; i < nSeqInfos; i++) {
        uint32_t pSeqInfoUnadjusted = file->GetWord(pSeqInfoPtrList + 4 + i * 4);
        uint32_t pSeqInfo = INFOoff + pSeqInfoUnadjusted;
        seqFileIDs[i] = pSeqInfoUnadjusted ? file->GetShort(pSeqInfo) : 0xFFFF;
        seqFileBnks[i] = file->GetShort(pSeqInfo + 4);
        // next bytes would be vol, cpr, ppr, and ply respectively, whatever the heck those last 3
        // stand for
    }

    uint32_t pBnkInfoPtrList = file->GetWord(INFOoff + 0x10) + INFOoff;
    uint32_t nBnkInfos = file->GetWord(pBnkInfoPtrList);
    std::vector<uint16_t> bnkFileIDs(nBnkInfos);
    std::vector<std::vector<uint16_t>> bnkWAs(nBnkInfos);
    for (uint32_t i = 0; i < nBnkInfos; i++) {
        uint32_t pBnkInfoUnadjusted = file->GetWord(pBnkInfoPtrList + 4 + i * 4);
        uint32_t pBnkInfo = INFOoff + pBnkInfoUnadjusted;
        bnkFileIDs[i] = pBnkInfoUnadjusted ? file->GetShort(pBnkInfo) : 0xFFFF;

        std::vector<uint16_t> ref;
        for (int j = 0; j < 4; j++) {
            uint16_t WANum = file->GetShort(pBnkInfo + 4 + (j * 2));
            if (WANum >= nWAs) {
                ref.push_back(0xFFFF);
            } else {
                ref.push_back(WANum);
            }
        }

        bnkWAs[i] = std::move(ref);
    }

    uint32_t pWAInfoList = file->GetWord(INFOoff + 0x14) + INFOoff;
    uint32_t nWAInfos = file->GetWord(pWAInfoList);
    std::vector<uint16_t> waFileIDs(nWAInfos);
    for (uint32_t i = 0; i < nWAInfos; i++) {
        uint32_t pWAInfoUnadjusted = file->GetWord(pWAInfoList + 4 + i * 4);
        uint32_t pWAInfo = INFOoff + pWAInfoUnadjusted;
        waFileIDs[i] = pWAInfoUnadjusted ? file->GetShort(pWAInfo) : 0xFFFF;
    }

    std::vector<NDSWaveArch *> WAs;
    {
        std::vector<uint16_t> vUniqueWAs;
        for (auto &v : bnkWAs) {
            vUniqueWAs.insert(std::end(vUniqueWAs), std::begin(v), std::end(v));
        }
        std::sort(std::begin(vUniqueWAs), std::end(vUniqueWAs));

        auto new_end = std::unique(vUniqueWAs.begin(), vUniqueWAs.end());
        vUniqueWAs.erase(new_end, std::end(vUniqueWAs));

        std::vector<bool> valid(nWAs);
        for (auto &WAid : vUniqueWAs) {
            if (WAid != 0xFFFF && WAid < nWAs) {
                valid[WAid] = true;
            }
        }

        for (uint32_t i = 0; i < nWAs; i++) {
            if (valid[i] != true || waFileIDs[i] == 0xFFFF) {
                WAs.push_back(nullptr);
                continue;
            }

            uint32_t offset = FAToff + 12 + waFileIDs[i] * 0x10;
            uint32_t pWAFatData = file->GetWord(offset) + baseOff;
            offset += 4;
            uint32_t fileSize = file->GetWord(offset);

            NDSWaveArch *NewNDSwa = new NDSWaveArch(file, pWAFatData, fileSize, waNames[i]);
            if (!NewNDSwa->LoadVGMFile()) {
                L_ERROR("Failed to load NDSWaveArch at {:#x}", pWAFatData);
                WAs.push_back(NULL);
                delete NewNDSwa;
                continue;
            }

            WAs.push_back(NewNDSwa);
        }
    }

    std::vector<std::pair<uint16_t, NDSInstrSet *>> BNKs;
    {
        std::vector<uint16_t> vUniqueBanks(seqFileBnks);
        sort(vUniqueBanks.begin(), vUniqueBanks.end());
        std::vector<uint16_t>::iterator new_end = unique(vUniqueBanks.begin(), vUniqueBanks.end());

        // for (uint32_t i=0; i<nBnks; i++)
        // for (uint32_t i=0; i<seqFileBnks.size(); i++)
        for (std::vector<uint16_t>::iterator iter = vUniqueBanks.begin(); iter != new_end; iter++) {
            if (*iter >= bnkFileIDs.size() /*0x1000*/ ||
                bnkFileIDs[*iter] ==
                    (uint16_t)-1)  // > 0x1000 is idiot test for Phoenix Wright, which had many
                                   // 0x1C80 values, as if they were 0xFFFF
                continue;
            uint32_t offset = FAToff + 12 + bnkFileIDs[*iter] * 0x10;
            uint32_t pBnkFatData = file->GetWord(offset) + baseOff;
            offset += 4;
            uint32_t fileSize = file->GetWord(offset);
            // if (bnkWAs[*iter][0] == (uint16_t)-1 || numWAs != 1)
            //	continue;
            NDSInstrSet *NewNDSInstrSet = new NDSInstrSet(
                file, pBnkFatData, fileSize, bnkNames[*iter] /*, WAs[bnkWAs[*iter][0]]*/);
            for (int i = 0; i < 4; i++)  // use first WA found.  Ideally, should load all WAs
            {
                short WAnum = bnkWAs[*iter][i];
                if (WAnum != -1)
                    NewNDSInstrSet->sampCollWAList.push_back(WAs[WAnum]);
                else
                    NewNDSInstrSet->sampCollWAList.push_back(NULL);
            }
            if (!NewNDSInstrSet->LoadVGMFile()) {
                L_ERROR("Failed to load NDSInstrSet at {:#x}", pBnkFatData);
            }
            std::pair<uint16_t, NDSInstrSet *> theBank(*iter, NewNDSInstrSet);
            BNKs.push_back(theBank);
        }
    }

    {
        // vector<uint16_t> vUniqueSeqs = vector<uint16_t>(seqFileIDs);
        // sort(vUniqueSeqs.begin(), vUniqueSeqs.end());
        // vector<uint16_t>::iterator new_end = unique(vUniqueBanks.begin(), vUniqueBanks.end());

        for (uint32_t i = 0; i < nSeqs; i++) {
            if (seqFileIDs[i] == (uint16_t)-1)
                continue;
            uint32_t offset = FAToff + 12 + seqFileIDs[i] * 0x10;
            uint32_t pSeqFatData = file->GetWord(offset) + baseOff;
            offset += 4;
            uint32_t fileSize = file->GetWord(offset);
            NDSSeq *NewNDSSeq = new NDSSeq(file, pSeqFatData, fileSize, seqNames[i]);
            if (!NewNDSSeq->LoadVGMFile()) {
                L_ERROR("Failed to load NDSSeq at {:#x}", pSeqFatData);
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
