/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SegSatScanner.h"
#include "SegSatSeq.h"
#include "ScannerManager.h"
#include "SegSatInstrSet.h"
#include "VGMMiscFile.h"

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
  searchForSequences(file);
  searchForInstrumentSets(file);
}
void SegSatScanner::searchForSequences(RawFile *file) {
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

    bool bParsedSeq = false;
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

      auto name = fmt::format("{} {:d}", file->name(), n);
      SegSatSeq* seq = new SegSatSeq(file, i + seqPtr, name);
      if (!seq->loadVGMFile())
        delete seq;
      else
        bParsedSeq = true;
    }
    if (bParsedSeq) {
      auto seqTable = new VGMMiscFile(SegSatFormat::name, file, i,
        2 + (numSeqs * 4), "Sega Saturn Seq Table");
      seqTable->addChild(i, 2, "Sequence Count");
      for (int j = 0; j < numSeqs; j++) {
        seqTable->addChild(i + 2 + (j * 4), 4, fmt::format("Sequence {:d} Pointer", j));
      }
      seqTable->loadVGMFile();
    }

    u32 lastSeqPtr = file->readWordBE(i + 2 + ((numSeqs-1) * 4));
    // Sanity check the pointer
    if (lastSeqPtr > fileLength)
      break;
    i += lastSeqPtr + minSeqSize;
  }
}

// Return true if a valid Sega Saturn instrument bank header/table is at `base`.
bool SegSatScanner::validateBankAt(RawFile* file, u32 base) {
  const u32 n = file->size();
  if (base + 8 > n) return false;

  // Read header (big-endian)
  const u16 ptrMixes = file->readShortBE(base + 0);
  const u16 ptrVel   = file->readShortBE(base + 2);
  const u16 ptrPegs  = file->readShortBE(base + 4);
  const u16 ptrPlfo  = file->readShortBE(base + 6);
  const u16 ptrInstr0  = file->readShortBE(base + 8);

  // Fast rejects on the first four words
  // 1) Even alignment is cheap to test and catches lots of junk.
  if ((ptrMixes | ptrVel | ptrPegs | ptrPlfo) & 1) return false;

  // Sanity check for huge values
  if (ptrMixes >= 0x1000 || ptrVel >= 0x1000 || ptrPegs >= 0x1000) return false;

  // 2) Strictly increasing (monotonic)
  if (!(ptrMixes < ptrVel && ptrVel < ptrPegs && ptrPegs < ptrPlfo)) return false;

  // 3) Unit-size (“multiple of _”) checks on the first 3 gaps.
  const u32 d0 = static_cast<u32>(ptrVel  ) - ptrMixes;  // vel - mixes
  const u32 d1 = static_cast<u32>(ptrPegs ) - ptrVel;    // pegs - vel
  const u32 d2 = static_cast<u32>(ptrPlfo ) - ptrPegs;   // plfo - pegs
  const u32 d3 = static_cast<u32>(ptrInstr0 ) - ptrPlfo; // instr0 - plfo
  if ((d0 % 0x12u) != 0) return false;
  if ((d1 % 0x0Au) != 0) return false;
  if ((d2 % 0x0Au) != 0) return false;
  if ((d3 % 0x04u) != 0) return false;

  // More sanity checks on sizes
  if (d0 > (20*0x12)) return false;   // assume no more than 20 mixer tables
  if (d1 > (20*0x0A)) return false;   // assume no more than 20 velocity level tables
  if (d2 > (20*0x0A)) return false;   // assume no more than 20 PEG tables
  if (d3 > (20*0x04)) return false;   // assume no more than 20 PLFO tables

  // 4) Instrument count (derived from ptrMixes).
  //    (ptrMixes - 8) must be even and >= 2 to fit at least one instrument.
  if (ptrMixes < 10) return false;            // < 0x0A
  if ((ptrMixes & 1) != 0) return false;      // makes (ptrMixes - 8) odd
  int numInstrs = static_cast<int>((ptrMixes - 8) / 2);
  if (numInstrs <= 0) return false;

  // Check for an improbably high instrument count
  static constexpr int kMaxInstrs = 256;
  if (numInstrs > kMaxInstrs) return false;

  // Ensure the instrument-pointer table itself is in-bounds.
  const u32 instrTblEnd = base + 8u + static_cast<u32>(numInstrs) * 2u;
  if (instrTblEnd > n) return false;

  // Now check the remaining gaps between instruments
  // Instrument gap rule: (delta % 0x20) == 0x04  -> use bit-test: (delta & 0x1F) == 0x04
  u32 prev = file->readShortBE(base + 8) - 4;
  for (int j = 0; j < numInstrs; ++j) {
    const u16 v = file->readShortBE(base + 8 + j * 2);
    if (v <= prev) return false;
    const u32 delta = static_cast<u32>(v) - prev;
    if ( (delta & 0x1Fu) != 0x04u ) return false;
    prev = v;
  }

  return true;
}

void SegSatScanner::searchForInstrumentSets(RawFile* file) {
  const u32 fileLength = file->size();
  if (fileLength < 10) return;

  for (u32 base = 0; base + 8 <= fileLength; /* increment at bottom */) {
    // Heuristic skip #1: early out on long zero runs.
    const u32 firstWord = (base + 4 <= fileLength) ? file->readWordBE(base) : 0;
    if (firstWord == 0) { base += 2; continue; }
    if (firstWord <= 0xFF) { base += 1; continue; }

    // Cheap header gates before full validation:
    const u16 ptrMixes = file->readShortBE(base + 0);
    if (ptrMixes < 0x000A || (ptrMixes & 1)) { ++base; continue; }

    const u16 ptrVel = (base + 2 + 2 <= fileLength) ? file->readShortBE(base + 2) : 0;
    if (ptrVel <= ptrMixes) { ++base; continue; }

    // Quick check that header pointers are monotonic
    const u16 ptrPegs = (base + 4 + 2 <= fileLength) ? file->readShortBE(base + 4) : 0;
    const u16 ptrPlfo = (base + 6 + 2 <= fileLength) ? file->readShortBE(base + 6) : 0;
    const u16 ptrInstr0 = (base + 8 + 2 <= fileLength) ? file->readShortBE(base + 8) : 0;
    if (!(ptrMixes < ptrVel && ptrVel < ptrPegs && ptrPegs < ptrPlfo && ptrPlfo < ptrInstr0)) {
      ++base;
      continue;
    }

    // Fast unit-size checks on header gaps before full pass
    const u32 d0 = static_cast<u32>(ptrVel)  - ptrMixes;
    const u32 d1 = static_cast<u32>(ptrPegs) - ptrVel;
    const u32 d2 = static_cast<u32>(ptrPlfo) - ptrPegs;
    const u32 d3 = static_cast<u32>(ptrInstr0) - ptrPlfo;
    if ((d0 % 0x12u) | (d1 % 0x0Au) | (d2 % 0x0Au) | (d3 % 0x04u)) { ++base; continue; }

    // Full validation (read instrument table lazily and bail early on mismatch)
    if (validateBankAt(file, base)) {
      u32 numInstrs = ((ptrMixes - 8) / 2);
      auto instrSet = new SegSatInstrSet(file, base, numInstrs);
      if (!instrSet->loadVGMFile()) {
        delete instrSet;
      }

      // We can safely skip ahead: the instrument table runs until base + ptrMixes
      // Jumping avoids re-scanning in the middle of a confirmed bank.
      base = std::min(base + instrSet->unLength, fileLength - 8);
    } else {
      ++base;
    }
  }
}
