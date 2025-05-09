/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SegSatScanner.h"
#include "SegSatSeq.h"
#include "ScannerManager.h"
#include <array>

namespace vgmtrans::scanners {
ScannerRegistration<SegSatScanner> s_segsat("SegSat");
}

std::array<u8, 4> uint32ToBytes(u32 num) {
  std::array<u8, 4> bytes;
  bytes[0] = (num >> 24) & 0xFF;
  bytes[1] = (num >> 16) & 0xFF;
  bytes[2] = (num >> 8) & 0xFF;
  bytes[3] = num & 0xFF;
  return bytes;
}

void SegSatScanner::scan(RawFile *file, void *info) {
  constexpr u32 minSeqSize = 16;

  u32 fileLength = file->size();

  for (u32 i = 0; i + 0x20 < fileLength; i++) {

    u32 firstWord = file->readWordBE(i);
    // If the first word is 0, skip the first 3 bytes
    if (firstWord == 0) {
      i += 2;
      continue;
    }
    // If the first three bytes are 0 and the last is non-zero, skip the first 2 bytes
    if (firstWord <= 0xFF) {
      i += 1;
      continue;
    }

    // We expect the first and third bytes to be 0, the second byte to be non-zero
    auto firstWordBytes = uint32ToBytes(firstWord);
    if (firstWordBytes[0] != 0 || firstWordBytes[1] == 0 || firstWordBytes[2] != 0)
      continue;

    // Calculate if the size of the header + a miniscule single sequence would exceed the file size.
    u16 numSeqs = firstWordBytes[1];
    if (2 + (numSeqs * 4) + minSeqSize > fileLength)
      continue;

    // We expect the first seq offset to be immediately after the header
    u32 firstSeqPtr = file->readWordBE(i + 2);
    if (firstSeqPtr != 2 + (numSeqs * 4))
      continue;

    // Check that sequence pointers don't exceed file size and that they are ordered
    u32 prevSeqPtr = 0;
    bool bInvalid = false;
    for (u16 s = 0; s < numSeqs; s++) {
      u32 seqPtr = file->readWordBE(i + 2 + (s * 4));
      if (seqPtr <= prevSeqPtr || (i + seqPtr + minSeqSize) > fileLength) {
        bInvalid = true;
        break;
      }
      prevSeqPtr = seqPtr;
    }
    if (bInvalid)
      continue;

    for (int n = 0; n < numSeqs; ++n) {
      u32 seqPtr = file->readWordBE(i + 2 + (n * 4));
      u16 tempoTrkPtr = 8;
      u16 numTempoEvents = file->readShortBE(i + seqPtr + 2);
      u16 firstNonTempoTrkPtr = file->readShortBE(i + seqPtr + 4);
      u16 loopedTempoEventPtr = file->readShortBE(i + seqPtr + 6);
      if (loopedTempoEventPtr >= firstNonTempoTrkPtr ||
          tempoTrkPtr + (numTempoEvents * 8) != firstNonTempoTrkPtr) {
        L_DEBUG("Unexpected tempo track pointer in prospective SegSat sequence");
        break;
      }

      SegSatSeq* seq = new SegSatSeq(file, i + seqPtr, "Sega Saturn Sequence");
      if (!seq->loadVGMFile())
        delete seq;
    }

    u32 lastSeqPtr = file->readWordBE(i + 2 + ((numSeqs-1) * 4));
    // Sanity check the pointer
    if (lastSeqPtr > fileLength)
      break;
    i += lastSeqPtr + minSeqSize;
  }
}