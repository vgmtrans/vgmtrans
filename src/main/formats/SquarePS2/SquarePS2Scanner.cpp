/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SquarePS2Seq.h"
#include "WD.h"
#include "ScannerManager.h"
namespace vgmtrans::scanners {
ScannerRegistration<SquarePS2Scanner> s_squareps2("SquarePS2");
}

#define SRCH_BUF_SIZE 0x20000

void SquarePS2Scanner::scan(RawFile *file, void *info) {
  searchForBGMSeq(file);
  searchForWDSet(file);
}

void SquarePS2Scanner::searchForBGMSeq(RawFile *file) {
  uint32_t nFileLength;
  nFileLength = file->size();
  for (uint32_t i = 0; i + 4 < nFileLength; i++) {
    if (file->get<u8>(i) == 'B' && file->get<u8>(i + 1) == 'G' && file->get<u8>(i + 2) == 'M' &&
      file->get<u8>(i + 3) == ' ') {
      if (file->readWord(i + 0x14) == 0 && file->readWord(i + 0x18) == 0 &&
        file->readWord(i + 0x1C) == 0) {
        uint8_t nNumTracks = (*file)[i + 8];
        uint32_t pos = i + 0x20;  //start at first track (fixed offset)
        bool bValid = true;
        for (int j = 0; j < nNumTracks; j++) {
          uint32_t trackSize = file->readWord(pos);  //get the track size (first word before track data)
          if (trackSize + pos + j > nFileLength || trackSize == 0 || trackSize > 0xFFFF) {
            bValid = false;
            break;
          }
          // jump to the next track
          pos += trackSize + 4;
        }

        BGMSeq *NewBGMSeq = new BGMSeq(file, i);
        if (!NewBGMSeq->loadVGMFile())
          delete NewBGMSeq;
      }
    }
  }
}

void SquarePS2Scanner::searchForWDSet(RawFile *file) {
  uint32_t numRegions, firstRgnPtr;

  size_t nFileLength = file->size();
  for (uint32_t i = 0; i + 0x3000 < nFileLength; i++) {
    if (file->get<u8>(i) == 'W' && file->get<u8>(i + 1) == 'D' && file->get<u8>(i + 3) < 0x03) {
      if (file->readWord(i + 0x14) == 0 && file->readWord(i + 0x18) == 0 &&
        file->readWord(i + 0x1C) == 0) {
        // check the data at the offset of the first region entry's sample pointer. It should be
        // 16 0x00 bytes in a row
        numRegions = file->readWord(i + 0xC);
        firstRgnPtr = file->readWord(i + 0x20);  //read the pointer to the first region set

        // sanity check
        if (numRegions == 0 || numRegions > 500)
          continue;

        if (firstRgnPtr <= 0x1000) {
          bool bValid = true;
          uint32_t offsetOfFirstSamp = i + firstRgnPtr + numRegions * 0x20;

          int zeroOffsetCounter = 0;

          // check that every region points to a valid sample by checking if first 16 bytes of
          // sample are 0
          for (unsigned int curRgn = 0; curRgn < numRegions; curRgn++) {
            // ignore the first nibble, it varies between versions but will be consistent this way
            uint32_t relativeRgnSampOffset =
              file->readWord(i + firstRgnPtr + curRgn * 0x20 + 4) & 0xFFFFFFF0;
            if (relativeRgnSampOffset < 0x10)
              relativeRgnSampOffset = 0;
            uint32_t rgnSampOffset = relativeRgnSampOffset + offsetOfFirstSamp;

            if (relativeRgnSampOffset == 0)
              zeroOffsetCounter++;

            // if there are at least 3 zeroOffsetCounters and more than half
            // /of all samples are offset 0, something is fishy.  Assume false-positive
            if (zeroOffsetCounter >= 3 && zeroOffsetCounter / (float) numRegions > 0.50) {
              bValid = false;
              break;
            }
            // plus 16 cause we read for 16 empty bytes
            // first 16 bytes of sample better be 0
            if (rgnSampOffset + 16 >= nFileLength ||
                file->readWord(rgnSampOffset) != 0 ||
                file->readWord(rgnSampOffset + 4) != 0 ||
                file->readWord(rgnSampOffset + 8) != 0 ||
                file->readWord(rgnSampOffset + 12) != 0) {
              bValid = false;
              break;
            }
          }

          // then there was a row of 16 00 bytes.  yay
          if (bValid) {
            WDInstrSet *instrset = new WDInstrSet(file, i);
            if (!instrset->loadVGMFile())
              delete instrset;
          }
        }
      }
    }
  }
}
