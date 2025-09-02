/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SegSatScanner.h"
#include "SegSatSeq.h"
#include "ScannerManager.h"
#include "SegSatInstrSet.h"
#include "VGMColl.h"
#include "VGMMiscFile.h"
#include <array>

namespace vgmtrans::scanners {
ScannerRegistration<SegSatScanner> s_segsat("SegSat");
}

bool isSsfFile(RawFile* file) {
  return file->extension() == "ssf" ||
         file->extension() == "minissf" ||
         file->extension() == "ssflib";
}

std::array<u8, 4> uint32ToBytes(u32 num) {
  std::array<u8, 4> bytes;
  bytes[0] = (num >> 24) & 0xFF;
  bytes[1] = (num >> 16) & 0xFF;
  bytes[2] = (num >> 8) & 0xFF;
  bytes[3] = num & 0xFF;
  return bytes;
}

// moveq      #0x0,D4                 78 00
// move.w     #0x100,D5w              3a 3c 01 00   ; will increment d4 by 0x100 for each irq count (0x20 * 8)
// move.b     (DAT_00001827,A6),D0b   10 2e 18 27   ; read irq counter
// andi.w     #0x3,D0w                02 40 00 03   ; mod irq counter by 4
// add.w      D0w,D0w                 d0 40         ; prep d0 as switch jump offset
BytePattern SegSatScanner::ptn_v1_28_handle_8_slots_per_irq(
   "\x78\x00\x3a\x3c\x01\x00\x10\x2e\x18\x27\x02\x40\x00\x03\xd0\x40",
   "xxxxxxxx??xxxxxx", 16);

// move.w     #0x000,D4w              38 3c 00 00       ; write initial slot offset, this is at offset 0x1412
// addi.w     #0x100,D4w              06 44 01 00       ; add 0x100 (0x20 * 8) - next set of 8 slots
// andi.w     #0x300,D4w              02 44 03 00       ; mod 0x400
// move.w     D4w,(LAB_00001412+2).l  33 c4 00 00 14 14 ; overwrite the first instruction at offset 0x1412!
//                                                      ; increment the initial slot offset by 0x100
BytePattern SegSatScanner::ptn_v2_08_self_modifying_8_slots_per_irq(
   "\x38\x3C\x00\x00\x06\x44\x01\x00\x02\x44\x03\x00\x33\xc4\x00\x00\x14\x14",
   "xx??xxxxxxxxxxxx??", 18);

// lea        (0x1000,A6),A4          49 ee 10 00
// movem.l    {  A5 A4},-(SP          48 e7 00 0c
// moveq      #0x1f,D7                7e 1f
// move.b     (0x34,A4),D0b           10 2c 00 34
BytePattern SegSatScanner::ptn_v2_20_handle_all_slots_per_irq(
   "\x49\xee\x10\x00\x48\xe7\x00\x0c\x7e\x1f\x10\x2c\x00\x34",
   "xxxxxxxxxxxxxx", 14);

void SegSatScanner::scan(RawFile *file, void *info) {
  if (isSsfFile(file)) {
    handleSsfFile(file);
  } else {
    SegSatDriverVer driverVer = SegSatDriverVer::Unknown;
    searchForSeqs(file, true);
    searchForInstrSets(file, driverVer, true);
  }
}

SegSatDriverVer SegSatScanner::determineVersion(RawFile* file) {
  u32 ptnOff;
  if (file->searchBytePattern(ptn_v1_28_handle_8_slots_per_irq, ptnOff)) {
    return SegSatDriverVer::V1_28;
  }
  if (file->searchBytePattern(ptn_v2_08_self_modifying_8_slots_per_irq, ptnOff)) {
    // If the last instruction self-modifies the last 2 bytes of the first instruction in the pattern
    if (file->readShortBE(ptnOff + 16) == ptnOff + 2) {
      // This test also succeeds on 1.33, so we'll probably need additional tests to distinguish
      return SegSatDriverVer::V2_08;
    }
  }
  if (file->searchBytePattern(ptn_v2_20_handle_all_slots_per_irq, ptnOff)) {
    return SegSatDriverVer::V2_20;
  }
  return SegSatDriverVer::V2_08;
}

void SegSatScanner::handleSsfFile(RawFile* file) {
  SegSatDriverVer ver = SegSatDriverVer::Unknown;
  auto instrSets = searchForInstrSets(file, ver, false);

  std::map<u8, SegSatInstrSet*> banks;
  if (instrSets.size() > 1) {
    for (u32 i = 0x500; i < 0x600; i+= 8) {
      u32 bankNumAndPtr = file->readWordBE(i);
      // The 4 bytes after offset are bank size, but we don't use it
      u8 bankNum = bankNumAndPtr >> 24;
      u32 bankPtr = bankNumAndPtr & 0x00FFFFFF;
      if (bankNum == 0xFF || bankPtr >= file->size() + 8)
        break;
      if (banks.contains(bankNum))
        continue;

      for (const auto instrSet : instrSets) {
        if (instrSet->dwOffset == bankPtr) {
          instrSet->assignBankNumber(bankNum);
          banks[bankNum] = instrSet;
        }
      }
    }
  }
  auto seqs = searchForSeqs(file, false);

  if (instrSets.size() == 0)
    return;

  for (const auto seq : seqs) {
    VGMColl* coll = new VGMColl(seq->name());
    coll->useSeq(seq);
    if (instrSets.size() == 1) {
      instrSets[0]->assignBankNumber(0);
      coll->addInstrSet(instrSets[0]);
    } else {
      auto referencedBanks = seq->referencedBanks();
      auto numRefBanks = referencedBanks.size();
      for (auto bankNum : referencedBanks) {
        SegSatInstrSet* bank = nullptr;
        auto it = banks.find(bankNum);
        if (it == banks.end())
          bank = instrSets[0];
        else
          bank = it->second;
        bank->assignBankNumber(numRefBanks == 1 ? 0 : bankNum);
        coll->addInstrSet(bank);
      }
    }
    if (!coll->load()) {
      delete coll;
    }
  }
}

std::vector<SegSatSeq*> SegSatScanner::searchForSeqs(RawFile *file, bool useMatcher) {
  constexpr u32 minSeqSize = 16;

  u32 fileLength = file->size();
  std::vector<SegSatSeq*> seqs;
  int seqTableCounter = 0;

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

      auto name = fmt::format("{} {:d}_{:d}", file->name(), seqTableCounter, n);
      SegSatSeq* seq = new SegSatSeq(file, i + seqPtr, name);
      if (!seq->loadVGMFile(useMatcher))
        delete seq;
      else {
        bParsedSeq = true;
        seqs.push_back(seq);
      }
    }
    if (bParsedSeq) {
      auto seqTable = new VGMMiscFile(SegSatFormat::name, file, i,
        2 + (numSeqs * 4), "Sega Saturn Seq Table");
      seqTable->addChild(i, 2, "Sequence Count");
      for (int j = 0; j < numSeqs; j++) {
        seqTable->addChild(i + 2 + (j * 4), 4, fmt::format("Sequence {:d} Pointer", j));
      }
      seqTable->loadVGMFile();
      seqTableCounter += 1;
    }

    u32 lastSeqPtr = file->readWordBE(i + 2 + ((numSeqs-1) * 4));
    // Sanity check the pointer
    if (lastSeqPtr > fileLength)
      break;
    i += lastSeqPtr + minSeqSize;
  }
  return seqs;
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
  if (d1 > (20*0x0A) && d1 != (100*0x0A)) return false;   // assume no more than 20 velocity level tables
                                                          // special exception for Tengai Makyou
  if (d2 > (30*0x0A)) return false;   // assume no more than 20 PEG tables
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

std::vector<SegSatInstrSet*> SegSatScanner::searchForInstrSets(RawFile* file, SegSatDriverVer& ver, bool useMatcher) {
  const u32 fileLength = file->size();
  if (fileLength < 10) return {};

  std::vector<SegSatInstrSet*> instrSets;
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
      if (ver == SegSatDriverVer::Unknown)
        ver = determineVersion(file);
      u32 numInstrs = ((ptrMixes - 8) / 2);
      auto instrSet = new SegSatInstrSet(file, base, numInstrs, ver);
      if (useMatcher ? !instrSet->loadVGMFile() : !instrSet->load())
        delete instrSet;
      else
        instrSets.push_back(instrSet);

      // We can safely skip ahead: the instrument table runs until base + ptrMixes
      // Jumping avoids re-scanning in the middle of a confirmed bank.
      base = std::min(base + instrSet->unLength, fileLength - 8);
    } else {
      ++base;
    }
  }
  return instrSets;
}
