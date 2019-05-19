/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SquarePS2Seq.h"
#include "WD.h"

#define SRCH_BUF_SIZE 0x20000

SquarePS2Scanner::SquarePS2Scanner(void) {}

SquarePS2Scanner::~SquarePS2Scanner(void) {}

void SquarePS2Scanner::Scan(RawFile *file, void *info) {
    SearchForBGMSeq(file);
    SearchForWDSet(file);
    return;
}

void SquarePS2Scanner::SearchForBGMSeq(RawFile *file) {
    uint32_t nFileLength;
    nFileLength = file->size();
    for (uint32_t i = 0; i + 4 < nFileLength; i++) {
        if ((*file)[i] == 'B' && (*file)[i + 1] == 'G' && (*file)[i + 2] == 'M' &&
            (*file)[i + 3] == ' ') {
            if (file->GetWord(i + 0x14) == 0 && file->GetWord(i + 0x18) == 0 &&
                file->GetWord(i + 0x1C) == 0) {
                uint8_t nNumTracks = (*file)[i + 8];
                uint32_t pos = i + 0x20;  // start at first track (fixed offset)
                bool bValid = true;
                for (int j = 0; j < nNumTracks; j++) {
                    uint32_t trackSize =
                        file->GetWord(pos);  // get the track size (first word before track data)
                    if (trackSize + pos + j > nFileLength || trackSize == 0 || trackSize > 0xFFFF) {
                        bValid = false;
                        break;
                    }
                    // jump to the next track
                    pos += trackSize + 4;
                }

                BGMSeq *NewBGMSeq = new BGMSeq(file, i);
                if (!NewBGMSeq->LoadVGMFile())
                    delete NewBGMSeq;
            }
        }
    }
}

void SquarePS2Scanner::SearchForWDSet(RawFile *file) {
    uint32_t numRegions, firstRgnPtr;

    float prevProPreRatio = file->GetProPreRatio();
    file->SetProPreRatio(1);

    uint32_t nFileLength = file->size();
    for (uint32_t i = 0; i + 0x3000 < nFileLength; i++) {
        if ((*file)[i] == 'W' && (*file)[i + 1] == 'D' && (*file)[i + 3] < 0x03) {
            if (file->GetWord(i + 0x14) == 0 && file->GetWord(i + 0x18) == 0 &&
                file->GetWord(i + 0x1C) == 0) {
                // check the data at the offset of the first region entry's sample pointer.  It
                // should be 16 0x00 bytes in a row
                numRegions = file->GetWord(i + 0xC);
                firstRgnPtr = file->GetWord(i + 0x20);  // read the pointer to the first region set

                // sanity check
                if (numRegions == 0 || numRegions > 500)
                    continue;

                if (firstRgnPtr <= 0x1000) {
                    bool bValid = true;
                    uint32_t offsetOfFirstSamp = i + firstRgnPtr + numRegions * 0x20;

                    int zeroOffsetCounter = 0;

                    // check that every region points to a valid sample by checking if first 16
                    // bytes of sample are 0
                    for (unsigned int curRgn = 0; curRgn < numRegions; curRgn++) {
                        // ignore the first nibble, it varies between versions but will be
                        // consistent this way
                        uint32_t relativeRgnSampOffset =
                            file->GetWord(i + firstRgnPtr + curRgn * 0x20 + 4) & 0xFFFFFFF0;
                        if (relativeRgnSampOffset < 0x10)
                            relativeRgnSampOffset = 0;
                        uint32_t rgnSampOffset = relativeRgnSampOffset + offsetOfFirstSamp;

                        if (relativeRgnSampOffset == 0)
                            zeroOffsetCounter++;

                        // if there are at least 3 zeroOffsetCounters and more than half
                        // /of all samples are offset 0, something is fishy.  Assume false-positive
                        if (zeroOffsetCounter >= 3 &&
                            zeroOffsetCounter / (float)numRegions > 0.50) {
                            bValid = false;
                            break;
                        }
                        // plus 16 cause we read for 16 empty bytes
                        // first 16 bytes of sample better be 0
                        if (rgnSampOffset + 16 >= nFileLength ||
                            file->GetWord(rgnSampOffset) != 0 ||
                            file->GetWord(rgnSampOffset + 4) != 0 ||
                            file->GetWord(rgnSampOffset + 8) != 0 ||
                            file->GetWord(rgnSampOffset + 12) != 0) {
                            bValid = false;
                            break;
                        }
                    }

                    // then there was a row of 16 00 bytes.  yay
                    if (bValid) {
                        WDInstrSet *instrset = new WDInstrSet(file, i);
                        if (!instrset->LoadVGMFile())
                            delete instrset;
                    }
                }
            }
        }
    }
    file->SetProPreRatio(prevProPreRatio);
}