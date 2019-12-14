/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "TriAcePS1Seq.h"
#include "TriAcePS1InstrSet.h"
#include "VGMColl.h"

#include <memory>

using namespace std;

#define DEFAULT_UFSIZE 0x100000

TriAcePS1Scanner::TriAcePS1Scanner(void) {}

TriAcePS1Scanner::~TriAcePS1Scanner(void) {}

void TriAcePS1Scanner::Scan(RawFile *file, void *info) {
    SearchForSLZSeq(file);
    return;
}

void TriAcePS1Scanner::SearchForSLZSeq(RawFile *file) {
    uint32_t nFileLength = file->size();
    for (uint32_t i = 0; i + 0x40 < nFileLength; i++) {
        uint32_t sig1 = file->GetWordBE(i);
        uint8_t mode;

        mode = sig1 & 0xFF;
        sig1 >>= 8;
        if (sig1 != 0x534C5A)  // "SLZ" in ASCII
            continue;
        if (mode > 0x03)
            continue;  // only SLZ v0-3 is supported
        // Note: SLZ v2 is used by a few tracks in Valkyrie Profile.

        uint16_t headerBytes = file->GetShort(i + 0x11);

        if (headerBytes != 0xFFFF)  // First two bytes of the sequence is always 0xFFFF
            continue;

        uint32_t size1 = file->GetWord(i + 4);  // unknown.  compressed size or something
        uint32_t size2 = file->GetWord(
            i + 8);  // uncompressed file size (size of resulting file after decompression)
        uint32_t size3 = file->GetWord(i + 12);  // unknown compressed file size or something

        if (size1 > 0x30000 || size2 > 0x30000 ||
            size3 > 0x30000)  // sanity check.  Sequences won't be > 0x30000 bytes
            continue;
        if (size1 > size2 ||
            size3 >
                size2)  // the compressed file size ought to be smaller than the decompressed size
            continue;

        TriAcePS1Seq *seq = TriAceSLZDecompress(file, i);
        if (!seq)
            return;
        if (i < 4 || file->GetWord(i - 4) == 0)
            return;

        // The size of the compressed file is 4 bytes behind the SLZ sig.
        // We need this to calculate the offset of the InstrSet, which comes immediately after the
        // SLZ'd sequence.
        vector<TriAcePS1InstrSet *> instrsets;
        SearchForInstrSet(file, instrsets);

        // uint32_t cfSize = file->GetWord(i-4);
        // TriAcePS1InstrSet* instrset = new TriAcePS1InstrSet(file, i-4 + cfSize);
        // if (!instrset->LoadVGMFile())
        //{
        //	delete instrset;
        //	return;
        //}
        if (!instrsets.size())
            return;

        std::string name = file->tag.HasTitle() ? file->tag.title : removeExtFromPath(file->name());
        VGMColl *coll = new VGMColl(name);
        coll->UseSeq(seq);
        for (uint32_t i = 0; i < instrsets.size(); i++)
            coll->AddInstrSet(instrsets[i]);
        if (!coll->Load()) {
            delete coll;
        }
    }
}

void TriAcePS1Scanner::SearchForInstrSet(RawFile *file, vector<TriAcePS1InstrSet *> &instrsets) {
    uint32_t nFileLength = file->size();
    for (uint32_t i = 4; i + 0x800 < nFileLength; i++) {
        uint8_t precedingByte = file->GetByte(i + 3);
        if (precedingByte != 0)
            continue;

        // The 32 byte value at offset 8 seems to be bank #.  In practice, i don't think it is ever
        // > 1
        if (file->GetWord(i + 8) > 0xFF)
            continue;

        uint16_t instrSectSize = file->GetShort(i + 4);
        // The instrSectSize should be more than the size of one instrdata block and not insanely
        // large
        if (instrSectSize <= 0x20 || instrSectSize > 0x4000)
            continue;
        // Make sure the size doesn't point beyond the end of the file
        if (i + instrSectSize > file->size() - 0x100)
            continue;

        uint32_t instrSetSize = file->GetWord(i);
        if (instrSetSize < 0x1000)
            continue;
        // The entire InstrSet size must be larger than the instrdata region, of course
        if (instrSetSize <= instrSectSize)
            continue;

        // First 0x10 bytes of sample section should be 0s
        if (file->GetWord(i + instrSectSize) != 0 || file->GetWord(i + instrSectSize + 4) != 0 ||
            file->GetWord(i + instrSectSize + 8) != 0 || file->GetWord(i + instrSectSize + 12) != 0)
            continue;
        // Last 4 bytes of Instr section should be 0xFFFFFFFF
        if (file->GetWord(i + instrSectSize - 4) != 0xFFFFFFFF)
            continue;

        TriAcePS1InstrSet *instrset = new TriAcePS1InstrSet(file, i);
        if (!instrset->LoadVGMFile()) {
            delete instrset;
            continue;
        }
        instrsets.push_back(instrset);
    }
    return;
}

// file is RawFile containing the compressed seq.  cfOff is the compressed file offset.
TriAcePS1Seq *TriAcePS1Scanner::TriAceSLZDecompress(RawFile *file, uint32_t cfOff) {
    uint8_t cmode = file->GetByte(cfOff + 3);    // compression mode
    uint32_t cfSize = file->GetWord(cfOff + 4);  // compressed file size
    uint32_t ufSize = file->GetWord(
        cfOff + 8);  // uncompressed file size (size of resulting file after decompression)
    uint32_t blockSize =
        file->GetWord(cfOff + 12);  // size of entire compressed block (slightly larger than cfSize)

    if (ufSize == 0)
        ufSize = DEFAULT_UFSIZE;

    uint8_t *uf = new uint8_t[ufSize];

    uint32_t ufOff = 0;
    cfOff += 0x10;
    if (cmode == 0) {
        ufOff += file->GetBytes(cfOff, ufSize, uf);
    } else {
        // The decompression code is based on CUE's SLZ decompressor.
        // compression mode: 0/STORE, 1/LZSS, 2/LZSS+RLE, 3/LZSS16
        bool bDone = false;
        int bits = (cmode == 3) ? 16 : 8;
        while (ufOff < ufSize && !bDone) {
            uint16_t cFlags;
            if (bits == 8)
                cFlags = file->GetByte(cfOff++);
            else {
                cFlags = file->GetShort(cfOff);
                cfOff += 2;
            }
            for (int i = 0; (i < bits) && (ufOff < ufSize); i++, cFlags >>= 1) {
                if (cFlags & 1)  // uncompressed byte, just copy it over
                {
                    uf[ufOff++] = file->GetByte(cfOff++);
                    if (bits == 16)
                        uf[ufOff++] = file->GetByte(cfOff++);
                } else  // compressed section
                {
                    uint8_t byte1 = file->GetByte(cfOff);
                    uint8_t byte2 = file->GetByte(cfOff + 1);
                    if (byte1 == 0 && byte2 == 0) {
                        bDone = true;
                        break;
                    }
                    cfOff += 2;

                    uint8_t bytesToRead;
                    if (cmode == 2 && byte2 >= 0xF0) {
                        if (byte2 == 0xF0) {
                            bytesToRead = byte1 + 0x13;
                            byte1 = file->GetByte(cfOff++);
                        } else {
                            bytesToRead = (byte2 & 0x0F) + 3;
                        }
                        for (; bytesToRead > 0; bytesToRead--)
                            uf[ufOff++] = byte1;
                    } else {
                        uint32_t backPtr = ufOff - (((byte2 & 0x0F) << 8) + byte1);
                        bytesToRead = (byte2 >> 4) + 3;

                        for (; bytesToRead > 0; bytesToRead--)
                            uf[ufOff++] = uf[backPtr++];
                    }
                }
            }
        }
    }
    if (ufOff > ufSize) {
        L_ERROR("Offset is out of bounds");
    }

    // If we had to use DEFAULT_UFSIZE because the uncompressed file size was not given (Valkyrie
    // Profile), then create a new buffer of the correct size now that we know it, and delete the
    // old one.
    if (ufSize == DEFAULT_UFSIZE) {
        uint8_t *newUF = new uint8_t[ufOff];
        memcpy(newUF, uf, ufOff);
        delete[] uf;
        uf = newUF;
    }
    // pRoot->UI_WriteBufferToFile("uncomp.raw", uf, ufOff);

    // Create the new virtual file, and analyze the sequence
    std::string name = file->tag.HasTitle() ? file->tag.title : removeExtFromPath(file->name());
    auto newVirtFile = std::make_shared<VirtFile>(uf, ufOff, name + std::string(" Sequence"),
                                                  file->GetParRawFileFullPath().c_str());

    TriAcePS1Seq *newSeq = new TriAcePS1Seq(newVirtFile.get(), 0, name);
    bool bLoadSucceed = newSeq->LoadVGMFile();

    newVirtFile->setUseLoaders(false);
    newVirtFile->setUseScanners(false);
    pRoot->SetupNewRawFile(newVirtFile);

    if (bLoadSucceed)
        return newSeq;
    else {
        delete newSeq;
        return NULL;
    }
}
