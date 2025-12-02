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
  };

  auto it = versionMap.find(versionStr);
  return it != versionMap.end() ? it->second : VERSION_UNDEFINED;
}

// TMNT2 (Z80)
// 02E9  ld hl,0xF801      21 01 F8
// 02EC  ld de,0xF800      11 00 F8
// 02EF  exx               D9
// 02F0  ld hl,0x55F7      21 F7 55
// 02F3  and 0x7F          E6 7F
// BytePattern KonamiTMNT2Scanner::ptn_LoadSeqTable("\x21\x01\xF8\x11\x00\xF8\xD9\x21\xF7\x55\xE6\x7F", "xxxxxxxx??xx", 12);

// 02F0  ld hl,0x55F7      21 F7 55
// 02F3  and 0x7F          E6 7F
// 02F5  rlca              07
// 02F6  ld e,a            5f
// 02F7  add hl,de         19
BytePattern KonamiTMNT2Scanner::ptn_tmnt2_LoadSeqTable("\x21\xF7\x55\xE6\x7F\x07\x5F\x19", "x??xxxxx", 8);

// ram:035f 26 00           LD         H,0x0
// ram:0361 29              ADD        HL,HL
// ram:0362 11 b2 5c        LD         DE,0x5cb2                                                5cb2 is song ptrs - 0x100 (0x5db2)
// ram:0365 19              ADD        HL,DE                                                    for song 01, hl == 0x100
// ram:0366 5e              LD         E=>DAT_ram_5cb2,(HL
// ram:0367 23              INC        HL
// ram:0368 56              LD         D,(HL=>DAT_ram_5cb3                                      = 21h    !
BytePattern KonamiTMNT2Scanner::ptn_simp_LoadSeqTable("\x26\x00\x29\x11\xB2\x5C\x19\x5E\x23\x56", "xxxx??xxxx", 10);


// ram:015c 5d              LD         E,L
// ram:015d 54              LD         D,H
// ram:015e 29              ADD        HL,HL
// ram:015f 19              ADD        HL,DE
// ram:0160 09              ADD        HL,BC
// ram:0161 11 e1 2a        LD         DE,0x2ae1
BytePattern KonamiTMNT2Scanner::ptn_moomesa_LoadSeqTable("\x5D\x54\x29\x19\x09\x11\xE1\x2A", "xxxxxx??", 8);

// ram:3f26 13              INC        DE                     # curOffset++
// ram:3f27 1a              LD         A,(DE)                 # read program num
// ram:3f28 21 7b 49        LD         HL,0x497b              # load instr table offset
// ram:3f2b 07              RLCA                              # programNum -> table index offset
// ram:3f2c 4f              LD         C,A
// ram:3f2d 09              ADD        HL,BC                  # instr table offset + program offset
// ram:3f2e 4e              LD         C,(HL=>instr_ptr_table
BytePattern KonamiTMNT2Scanner::ptn_tmnt2_LoadInstrTable("\x13\x1A\x21\x7B\x49\x07\x4F\x09\x4E", "xxx??xxxx", 9);

// ram:44f6 4f              LD         param_1,A
// ram:44f7 06 00           LD         param_1,0x0
// ram:44f9 dd 7e 18        LD         A,(IX+param_4->added_to_note_0)
// ram:44fc 07              RLCA
// ram:44fd 5f              LD         curOffset,A
// ram:44fe 50              LD         curOffset,param_1
// ram:44ff 21 b7 49        LD         HL, 0x49B7                                           drum_tables
// ram:4502 19              ADD        HL, curOffset                                             octave (chan state val) determin
//                                                                                              note determines index in table
BytePattern KonamiTMNT2Scanner::ptn_tmnt2_LoadDrumTable("\x4F\x06\x00\xDD\x7E\x18\x07\x5F\x50\x21\xB7\x49\x19", "xxxxx?xxxx??x", 13);


// ram:1996 13              INC        curOffset
// ram:1997 1a              LD         A,(curOffset)                                    read program num
// ram:1998 d9              EXX
// ram:1999 cb 7f           BIT        0x7,A
// ram:199b ca a6 19        JP         Z,program_num_<=_0x7F
// ram:199e 21 af 24        LD         HL,ym2151_instrs
// ram:19a1 e6 7f           AND        0x7f
BytePattern KonamiTMNT2Scanner::ptn_tmnt2_LoadYM2151InstrTable("\x13\x1A\xD9\xCB\x7F\xCA\xA6\x19\x21\xAF\x24\xE6\x7F", "xxxxxx??x??xx", 13);


// ram:1b76 44              LD         B,H
// ram:1b77 4d              LD         C,L
// ram:1b78 21 bb 25        LD         HL,ym2151_instrs                                 = ram:27bb
// ram:1b7b 09              ADD        HL,BC
// ram:1b7c 4e              LD         C,(HL=>ym2151_instrs)                            = ram:27bb
// ram:1b7d 23              INC        HL
// ram:1b7e 46              LD         B,(HL=>ym2151_instrs+1)
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
    // if (i >= minInstrPtr || i >= drumTableAddr)
      // break;
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


  // std::vector<uint32_t> trackOffsets;
  // if (sequenceOffset != 0) {
  //   trackOffsets.push_back(sequenceOffset);
  // }
  //
  // if (trackOffsets.empty()) {
  //   trackOffsets.push_back(0);
  // }
  //
  // for (int i = 0; i < seqPtrs.size(); ++i) {

  // }
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
  // u32 i = seqTableAddr;
  for (u32 i = seqTableAddr; !seqPtrs.contains(i); i += 2) {
    const u16 seqPtr = programRom->readShort(i);
    seqPtrs.insert(seqPtr);
    seqTable->addChild(i, 2, "Sequence Pointer");
  }
  // seqTable->unLength = i - seqTableAddr;

  // if (!seqTable->loadVGMFile()) {
  //   delete seqTable;
  //   return;
  // }

  // enum SeqType : u8 { ALL_CHANS = 0, RESERVE_CHANS = 1 };
  std::vector<KonamiTMNT2Seq*> seqs;
  int i = 0;
  for (u16 seqPtr : seqPtrs) {
    auto seqType = static_cast<KonamiTMNT2Seq::SeqType>(programRom->readByte(seqPtr));

    int numYM3151Tracks;
    int numK053260Tracks;
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
    auto totalTracks = numYM3151Tracks + numK053260Tracks;
    auto trkList = seqTable->addChild(seqPtr, 1 + (totalTracks * 2), "Seq Track List");
    trkList->addChild(seqPtr, 1, "Seq Type");
    std::vector<u32> ym2151TrkPtrs = {};
    std::vector<u32> k053260TrkPtrs;
    ym2151TrkPtrs.reserve(numYM3151Tracks);
    k053260TrkPtrs.reserve(numK053260Tracks);
    for (int i = 0; i < numYM3151Tracks; ++i) {
      u16 offset = seqPtr + 1 + (i * 2);
      trkList->addChild(offset, 2, "YM2151 Track Pointer");
      ym2151TrkPtrs.push_back(programRom->readShort(offset));
    }
    for (int i = 0; i < numK053260Tracks; ++i) {
      u16 offset = seqPtr + 1 + ((numYM3151Tracks + i) * 2);
      trkList->addChild(offset, 2, "K053260 Track Pointer");
      k053260TrkPtrs.push_back(programRom->readShort(offset));
    }

    const std::string sequenceName = fmt::format("{} seq {}", gameName, i++);
    u16 start = std::min(
      std::ranges::min(ym2151TrkPtrs),
      std::ranges::min(k053260TrkPtrs)
    );
    // u16 start = std::ranges::min(k053260TrkPtrs);
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