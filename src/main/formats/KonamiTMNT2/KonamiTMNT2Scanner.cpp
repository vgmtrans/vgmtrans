/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiTMNT2Scanner.h"
#include "KonamiTMNT2Seq.h"
#include "MAMELoader.h"
#include "BytePattern.h"
#include "KonamiTMNT2Instr.h"
#include "KonamiTMNT2OPMInstr.h"
#include "VGMColl.h"
#include "VGMMiscFile.h"

#include <spdlog/fmt/fmt.h>
#include <vector>

KonamiTMNT2FormatVer konamiTMNT2VersionEnum(const std::string &versionStr) {
  static const std::unordered_map<std::string, KonamiTMNT2FormatVer> versionMap = {
    {"tmnt2", TMNT2},
    {"ssriders", SSRIDERS},
    {"vendetta", VENDETTA},
  };

  auto it = versionMap.find(versionStr);
  return it != versionMap.end() ? it->second : VERSION_UNDEFINED;
}

// 02F0  ld hl,0x55F7      21 F7 55
// 02F3  and 0x7F          E6 7F
// 02F5  rlca              07
// 02F6  ld e,a            5f
// 02F7  add hl,de         19
BytePattern KonamiTMNT2Scanner::ptn_tmnt2_LoadSeqTable("\x21\xF7\x55\xE6\x7F\x07\x5F\x19", "x??xxxxx", 8);

// 035F  ld h,0x00         26 00
// 0361  add hl,hl         29
// 0362  ld de,0x5CB2      11 B2 5C      # 5CB2 is song ptrs - 0x100 (0x5DB2)
// 0365  add hl,de         19
// 0366  ld e,(hl)         5E
// 0367  inc hl            23
// 0368  ld d,(hl)         56
BytePattern KonamiTMNT2Scanner::ptn_simp_LoadSeqTable("\x26\x00\x29\x11\xB2\x5C\x19\x5E\x23\x56", "xxxx??xxxx", 10);


// 015C  ld e,l            5D
// 015D  ld d,h            54
// 015E  add hl,hl         29
// 015F  add hl,de         19
// 0160  add hl,bc         09
// 0161  ld de,0x2AE1      11 E1 2A
BytePattern KonamiTMNT2Scanner::ptn_moomesa_LoadSeqTable("\x5D\x54\x29\x19\x09\x11\xE1\x2A", "xxxxxx??", 8);

// 3F26  inc de            13          # curOffset++
// 3F27  ld a,(de)         1A          # read program num
// 3F28  ld hl,0x497B      21 7B 49    # load instr table offset
// 3F2B  rlca              07          # programNum -> table index offset
// 3F2C  ld c,a            4F
// 3F2D  add hl,bc         09          # instr table offset + program offset
// 3F2E  ld c,(hl)         4E          # instr_ptr_table
BytePattern KonamiTMNT2Scanner::ptn_tmnt2_LoadInstrTable("\x13\x1A\x21\x7B\x49\x07\x4F\x09\x4E", "xxx??xxxx", 9);

// 44F6  ld param_1,a      4F
// 44F7  ld param_1,0x00   06 00
// 44F9  ld a,(ix+0x18)    DD 7E 18
// 44FC  rlca              07
// 44FD  ld curOffset,a    5F
// 44FE  ld curOffset,b    50
// 44FF  ld hl,0x49B7      21 B7 49    # drum_tables
// 4502  add hl,curOffset  19
BytePattern KonamiTMNT2Scanner::ptn_tmnt2_LoadDrumTable("\x4F\x06\x00\xDD\x7E\x18\x07\x5F\x50\x21\xB7\x49\x19", "xxxxx?xxxx??x", 13);

// 1996  inc curOffset             13
// 1997  ld a,(curOffset)          1A          # read program num
// 1998  exx                       D9
// 1999  bit 7,a                   CB 7F
// 199B  jp z,program_num_<=_0x7F  CA A6 19
// 199E  ld hl,0x24AF.             21 AF 24.   # ym2151_instrs
// 19A1  and 0x7F                  E6 7F
BytePattern KonamiTMNT2Scanner::ptn_tmnt2_LoadYM2151InstrTable("\x13\x1A\xD9\xCB\x7F\xCA\xA6\x19\x21\xAF\x24\xE6\x7F", "xxxxxx??x??xx", 13);

// 1B76  ld b,h                  44
// 1B77  ld c,l                  4D
// 1B78  ld hl,0x25BB            21 BB 25      # ym2151_instrs
// 1B7B  add hl,bc               09
// 1B7C  ld c,(hl)               4E
// 1B7D  inc hl                  23
// 1B7E  ld b,(hl)               46
BytePattern KonamiTMNT2Scanner::ptn_ssriders_LoadYM2151InstrTable("\x44\x4D\x21\xBB\x25\x09\x4E\x23\x46", "xxx??xxxx", 9);



void KonamiTMNT2Scanner::scan(RawFile * /*file*/, void *info) {
  auto *gameEntry = static_cast<MAMEGame *>(info);
  if (gameEntry == nullptr) {
    return;
  }
  KonamiTMNT2FormatVer fmtVer = konamiTMNT2VersionEnum(gameEntry->fmt_version_str);
  if (fmtVer == VERSION_UNDEFINED) {
    L_ERROR("XML entry uses an undefined format version: {}", gameEntry->fmt_version_str);
    return;
  }

  MAMERomGroup *programRomGroup = gameEntry->getRomGroupOfType("soundcpu");
  MAMERomGroup *sampsRomGroup = gameEntry->getRomGroupOfType("sound");
  if (!programRomGroup || !sampsRomGroup || !programRomGroup->file || !sampsRomGroup->file)
    return;

  RawFile* programRom = programRomGroup->file;
  RawFile* samplesRom = sampsRomGroup->file;

  u32 loadSeqTableAddr;
  u16 seqTableAddr;
  if (programRom->searchBytePattern(ptn_tmnt2_LoadSeqTable, loadSeqTableAddr)) {
    seqTableAddr = programRom->readShort(loadSeqTableAddr + 1);
  }
  else if (programRom->searchBytePattern(ptn_simp_LoadSeqTable, loadSeqTableAddr)) {
    seqTableAddr = programRom->readShort(loadSeqTableAddr + 4) + 0x100;
  }
  else if (programRom->searchBytePattern(ptn_moomesa_LoadSeqTable, loadSeqTableAddr)) {
    seqTableAddr = programRom->readShort(loadSeqTableAddr + 6) + 0xE;
  }
  else {
    return;
  }

  auto seqs = loadSeqTable(programRom, seqTableAddr, fmtVer, gameEntry->name);

  u32 loadInstrTableAddr;
  u16 instrTableAddrK053260 = 0;
  u16 drumTableAddr = 0;
  u16 instrTableAddrYM2151;
  if (programRom->searchBytePattern(ptn_tmnt2_LoadInstrTable, loadInstrTableAddr)) {
    instrTableAddrK053260 = programRom->readShort(loadInstrTableAddr + 3);
  }
  if (programRom->searchBytePattern(ptn_tmnt2_LoadDrumTable, loadInstrTableAddr)) {
    drumTableAddr = programRom->readShort(loadInstrTableAddr + 10);
  }
  if (programRom->searchBytePattern(ptn_tmnt2_LoadYM2151InstrTable, loadInstrTableAddr)) {
    instrTableAddrYM2151 = programRom->readShort(loadInstrTableAddr + 9);
  } else if (programRom->searchBytePattern(ptn_ssriders_LoadYM2151InstrTable, loadInstrTableAddr)) {
    instrTableAddrYM2151 = programRom->readShort(loadInstrTableAddr + 3);
  }
  if (instrTableAddrK053260 == 0 || drumTableAddr == 0 || instrTableAddrYM2151 == 0) {
    return;
  }
  std::vector<u32> instrPtrs;
  std::vector<u32> drumTablePtrs;
  u32 minInstrPtr = -1;
  u32 minDrumPtr = -1;
  for (int i = instrTableAddrK053260; i < minInstrPtr && i < drumTableAddr; i += 2) {
    u32 instrInfoPtr = programRom->readShort(i);
    minInstrPtr = std::min(minInstrPtr, instrInfoPtr);
    instrPtrs.push_back(instrInfoPtr);
  }

  for (int i = drumTableAddr;; i += 2) {
    u32 drumInfoPtr = programRom->readShort(i);
    minDrumPtr = std::min(minDrumPtr, drumInfoPtr);
    if (i == minDrumPtr)
      break;
    drumTablePtrs.push_back(drumInfoPtr);
  }

  std::vector<konami_tmnt2_instr_info> instrInfos;
  instrInfos.reserve(instrPtrs.size());
  for (u32 instrInfoPtr : instrPtrs) {
    konami_tmnt2_instr_info info;
    programRom->readBytes(instrInfoPtr, sizeof(konami_tmnt2_instr_info), &info);
    instrInfos.push_back(info);
  }

  std::vector<std::vector<konami_tmnt2_drum_info>> drumTables;
  std::unordered_set drumTablePtrSet(drumTablePtrs.begin(), drumTablePtrs.end());
  std::unordered_set<u32> drumInfoPtrSet {};
  for (auto drumTablePtr : drumTablePtrs) {
    std::vector<konami_tmnt2_drum_info> drumInfos;
    minDrumPtr = -1;
    u32 i = drumTablePtr;
    do {
      u32 drumInfoPtr = programRom->readShort(i);
      if (drumInfoPtr > i + 0x1000)
        break;
      if (i >= minDrumPtr)
        break;
      minDrumPtr = std::min(minDrumPtr, drumInfoPtr);
      konami_tmnt2_drum_info info;
      programRom->readBytes(drumInfoPtr, sizeof(konami_tmnt2_drum_info), &info);
      drumInfos.push_back(info);
      drumInfoPtrSet.insert(drumInfoPtr);
      i += 2;
    } while (!drumTablePtrSet.contains(i) && !drumInfoPtrSet.contains(i));
    drumTables.push_back(drumInfos);
  }

  std::string instrSetName = fmt::format("{} instrument set", gameEntry->name);

  auto instrSet = new KonamiTMNT2SampleInstrSet(
    programRom,
    instrTableAddrK053260,
    instrTableAddrK053260,
    drumTableAddr,
    instrInfos,
    drumTables,
    instrSetName,
    fmtVer
  );
  if (!instrSet->loadVGMFile()) {
    delete instrSet;
    instrSet = nullptr;
  }

  std::string sampCollName = fmt::format("{} sample collection", gameEntry->name);

  auto sampcoll = new KonamiTMNT2SampColl(
    samplesRom,
    instrSet,
    instrInfos,
    drumTables,
    0,
    static_cast<uint32_t>(samplesRom->size()),
    sampCollName
  );
  if (!sampcoll->loadVGMFile()) {
    delete sampcoll;
    sampcoll = nullptr;
  }

  auto opmInstrSet = new KonamiTMNT2OPMInstrSet(programRom, fmtVer, instrTableAddrYM2151, "YM2151 Instrument Set");
  if (!opmInstrSet->loadVGMFile()) {
    delete opmInstrSet;
    opmInstrSet = nullptr;
  }

  for (auto seq : seqs) {
    VGMColl* coll = new VGMColl(seq->name());

    coll->useSeq(seq);
    coll->addInstrSet(opmInstrSet);
    coll->addInstrSet(instrSet);
    coll->addSampColl(sampcoll);
    if (!coll->load()) {
      delete coll;
    }
  }
}

std::vector<KonamiTMNT2Seq*> KonamiTMNT2Scanner::loadSeqTable(
  RawFile* programRom,
  u32 seqTableAddr,
  KonamiTMNT2FormatVer fmtVer,
  std::string& gameName
) {
  VGMMiscFile *seqTable = new VGMMiscFile(
    KonamiTMNT2Format::name,
    programRom,
    seqTableAddr,
    0,
    "Sequence Table"
  );
  std::set<u16> seqPtrs;
  for (u32 i = seqTableAddr; !seqPtrs.contains(i); i += 2) {
    const u16 seqPtr = programRom->readShort(i);
    seqPtrs.insert(seqPtr);
    seqTable->addChild(i, 2, "Sequence Pointer");
  }

  std::vector<KonamiTMNT2Seq*> seqs;
  int i = 0;
  for (u16 seqPtr : seqPtrs) {
    int numYM3151Tracks;
    int numK053260Tracks;
    if (fmtVer == VENDETTA) {
      numYM3151Tracks = 8;
      numK053260Tracks = 4;
    } else {
      auto seqType = static_cast<KonamiTMNT2Seq::SeqType>(programRom->readByte(seqPtr));

      if (fmtVer == SSRIDERS) {
        switch (seqType) {
          case 0:
            numYM3151Tracks = 6;
            numK053260Tracks = 2;
            break;
          case 1:
            numYM3151Tracks = 7;
            numK053260Tracks = 3;
            break;
          case 2:
            numYM3151Tracks = 8;
            numK053260Tracks = 3;
            break;
          case 3:
          default:
            numYM3151Tracks = 8;
            numK053260Tracks = 4;
            break;
        }
      } else {
        // int numTrkPtrs = seqType == KonamiTMNT2Seq::ALL_CHANS ? 12 : 9;
        switch (seqType) {
          case 0:
            numYM3151Tracks = 8;
            numK053260Tracks = 4;
            break;
          case 1:
          default:
            numYM3151Tracks = 6;
            numK053260Tracks = 3;
            break;
        }
      }
    }
    u32 seqTypeLength = (fmtVer == VENDETTA) ? 0 : 1;
    auto totalTracks = numYM3151Tracks + numK053260Tracks;
    auto trkList = seqTable->addChild(seqPtr, seqTypeLength + (totalTracks * 2), "Seq Track List");
    if (seqTypeLength)
      trkList->addChild(seqPtr, 1, "Seq Type");
    std::vector<u32> ym2151TrkPtrs = {};
    std::vector<u32> k053260TrkPtrs;
    ym2151TrkPtrs.reserve(numYM3151Tracks);
    k053260TrkPtrs.reserve(numK053260Tracks);
    for (int i = 0; i < numYM3151Tracks; ++i) {
      u16 offset = seqPtr + seqTypeLength + (i * 2);
      trkList->addChild(offset, 2, "YM2151 Track Pointer");
      ym2151TrkPtrs.push_back(programRom->readShort(offset));
    }
    for (int i = 0; i < numK053260Tracks; ++i) {
      u16 offset = seqPtr + seqTypeLength + ((numYM3151Tracks + i) * 2);
      trkList->addChild(offset, 2, "K053260 Track Pointer");
      k053260TrkPtrs.push_back(programRom->readShort(offset));
    }

    const std::string sequenceName = fmt::format("{} seq {}", gameName, i++);
    u16 start = std::min(
      std::ranges::min(ym2151TrkPtrs),
      std::ranges::min(k053260TrkPtrs)
    );

    auto *sequence = new KonamiTMNT2Seq(programRom, fmtVer, start, ym2151TrkPtrs, k053260TrkPtrs, sequenceName);
    if (!sequence->loadVGMFile()) {
      delete sequence;
    }
    seqs.push_back(sequence);
  }
  auto lastSeq = seqTable->children().back();
  seqTable->unLength = (lastSeq->dwOffset + lastSeq->unLength) - seqTable->dwOffset;
  if (!seqTable->loadVGMFile()) {
    delete seqTable;
  }
  return seqs;
}