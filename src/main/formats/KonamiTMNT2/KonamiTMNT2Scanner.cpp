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
#include "KonamiVendettaInstr.h"
#include "VGMColl.h"
#include "VGMMiscFile.h"

#include <optional>
#include <utility>
#include <spdlog/fmt/fmt.h>
#include <vector>

KonamiTMNT2FormatVer konamiTMNT2VersionEnum(const std::string &versionStr) {
  static const std::unordered_map<std::string, KonamiTMNT2FormatVer> versionMap = {
    {"tmnt2", TMNT2},
    {"ssriders", SSRIDERS},
    {"vendetta", VENDETTA},
    {"xexex", XEXEX},
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

  switch (fmtVer) {
    case TMNT2:
    case SSRIDERS:
      scanTMNT2(programRomGroup, sampsRomGroup, fmtVer, gameEntry->name);
      break;
    case VENDETTA:
      scanVendetta(programRomGroup, sampsRomGroup, fmtVer, gameEntry->name);
      break;
    case XEXEX:
      break;
  }
}

std::vector<KonamiTMNT2Seq*> KonamiTMNT2Scanner::loadSeqTable(
  RawFile* programRom,
  u32 seqTableAddr,
  KonamiTMNT2FormatVer fmtVer,
  u8 defaultTickSkipInterval,
  u8 clkb,
  const std::string& gameName
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
    if (fmtVer == VENDETTA && seqPtrs.contains(seqPtr))
      break;
    seqPtrs.insert(seqPtr);
    seqTable->addChild(i, 2, "Sequence Pointer");
  }

  std::vector<KonamiTMNT2Seq*> seqs;
  int i = 0;
  for (u16 seqPtr : seqPtrs) {
    auto getTrackCounts = [&](KonamiTMNT2FormatVer version, u16 seqOffset) {
      if (version == VENDETTA) {
        return std::pair{8, 4};
      }

      auto seqType = static_cast<KonamiTMNT2Seq::SeqType>(programRom->readByte(seqOffset));
      if (version == SSRIDERS) {
        switch (seqType) {
          case 0: return std::pair{6, 2};
          case 1: return std::pair{7, 3};
          case 2: return std::pair{8, 3};
          case 3:
          default: return std::pair{8, 4};
        }
      }

      // int numTrkPtrs = seqType == KonamiTMNT2Seq::ALL_CHANS ? 12 : 9;
      return seqType == 0 ? std::pair{8, 4} : std::pair{6, 3};
    };

    auto [numYM3151Tracks, numK053260Tracks] = getTrackCounts(fmtVer, seqPtr);
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

    auto *sequence = new KonamiTMNT2Seq(
      programRom,
      fmtVer,
      start,
      ym2151TrkPtrs,
      k053260TrkPtrs,
      defaultTickSkipInterval,
      clkb,
      sequenceName
    );
    if (!sequence->loadVGMFile()) {
      delete sequence;
    } else {
      seqs.push_back(sequence);
    }
  }
  auto lastSeq = seqTable->children().back();
  seqTable->unLength = (lastSeq->dwOffset + lastSeq->unLength) - seqTable->dwOffset;
  if (!seqTable->loadVGMFile()) {
    delete seqTable;
  }
  return seqs;
}

std::vector<std::vector<konami_tmnt2_drum_info>> KonamiTMNT2Scanner::loadDrumTablesTMNT2(
  RawFile* programRom,
  std::vector<u32> drumTablePtrs,
  u32 minDrumPtr
) {
  std::vector<std::vector<konami_tmnt2_drum_info>> drumTables;
  std::unordered_set drumTablePtrSet(drumTablePtrs.begin(), drumTablePtrs.end());
  std::unordered_set<u32> drumInfoPtrSet {};
  for (auto drumTablePtr : drumTablePtrs) {
    std::vector<konami_tmnt2_drum_info> drumInfos;
    minDrumPtr =  std::numeric_limits<u32>::max();
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
  return drumTables;
}

void KonamiTMNT2Scanner::scanTMNT2(
  MAMERomGroup* programRomGroup,
  MAMERomGroup* sampsRomGroup,
  KonamiTMNT2FormatVer fmtVer,
  const std::string& name
) {
  RawFile* programRom = programRomGroup->file;
  RawFile* samplesRom = sampsRomGroup->file;

  auto readTableAddr = [&](const BytePattern &pattern, u32 offset, u16 addend = 0)
    -> std::optional<u16> {
    u32 matchOffset;
    if (!programRom->searchBytePattern(pattern, matchOffset)) {
      return std::nullopt;
    }
    return static_cast<u16>(programRom->readShort(matchOffset + offset) + addend);
  };

  std::optional<u16> seqTableAddr = readTableAddr(ptn_tmnt2_LoadSeqTable, 1);
  if (!seqTableAddr)
    seqTableAddr = readTableAddr(ptn_simp_LoadSeqTable, 4, 0x100);
  if (!seqTableAddr)
    seqTableAddr = readTableAddr(ptn_moomesa_LoadSeqTable, 6, 0xE);
  if (!seqTableAddr) {
    return;
  }

  u32 defaultTickSkipInterval = 0;
  programRomGroup->getHexAttribute("default_tick_skip_interval", &defaultTickSkipInterval);
  u32 clkb = 0xF2;
  programRomGroup->getHexAttribute("CLKB", &clkb);

  auto seqs = loadSeqTable(
    programRom,
    *seqTableAddr,
    fmtVer,
    defaultTickSkipInterval,
    clkb & 0xFF,
    name
  );

  u32 instrTableAddrK053260, drumTableAddr, instrTableAddrYM2151;
  if (!programRomGroup->getHexAttribute("k053260_instr_table", &instrTableAddrK053260)) {
    instrTableAddrK053260 = readTableAddr(ptn_tmnt2_LoadInstrTable, 3).value_or(0);
  }
  if (!programRomGroup->getHexAttribute("k053260_drum_table", &drumTableAddr)) {
    drumTableAddr = readTableAddr(ptn_tmnt2_LoadDrumTable, 10).value_or(0);
  }
  if (!programRomGroup->getHexAttribute("k053260_instr_table", &instrTableAddrYM2151)) {
    instrTableAddrYM2151 = readTableAddr(ptn_tmnt2_LoadYM2151InstrTable, 9).value_or(0);
    if (instrTableAddrYM2151 == 0) {
      instrTableAddrYM2151 = readTableAddr(ptn_ssriders_LoadYM2151InstrTable, 3).value_or(0);
    }
  }
  if (instrTableAddrK053260 == 0 || drumTableAddr == 0 || instrTableAddrYM2151 == 0) {
    return;
  }
  std::vector<u32> instrPtrs;
  std::vector<u32> drumTablePtrs;
  u32 minInstrPtr = std::numeric_limits<u32>::max();
  u32 minDrumPtr =  std::numeric_limits<u32>::max();
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

  std::vector<std::vector<konami_tmnt2_drum_info>> drumTables = loadDrumTablesTMNT2(
    programRom,
    drumTablePtrs,
    minDrumPtr
  );

  std::string instrSetName = fmt::format("{} instrument set", name);

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


  // Consolidate the melodic and drum instruments into a single sample_info vector
  std::vector<KonamiTMNT2SampColl::sample_info> sampInfos;
  for (auto const& instrInfo : instrInfos) {
    sampInfos.emplace_back(KonamiTMNT2SampColl::sample_info::makeSampleInfo(instrInfo));
  }

  for (auto const& inner : drumTables) {
    for (auto const& drumInfo : inner) {
      sampInfos.emplace_back(KonamiTMNT2SampColl::sample_info::makeSampleInfo(drumInfo));
    }
  }

  std::string sampCollName = fmt::format("{} sample collection", name);

  auto sampcoll = new KonamiTMNT2SampColl(
    samplesRom,
    sampInfos,
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

konami_vendetta_drum_info KonamiTMNT2Scanner::parseVendettaDrum(
  RawFile* programRom,
  u16& offset,
  const vendetta_sub_offsets& subOffsets
) {
  // Each drum is defined by a 3 byte instrument data block, followed by actual Z80 instructions
  // We will store the meaning of those instructions in a konami_vendetta_drum_info instance.
  konami_vendetta_drum_info drumInfo;
  offset += sizeof(konami_vendetta_instr_k053260);
  u16 hl = 0;
  u8 a = 0;
  while (offset < programRom->size()) {
    u8 opcode = programRom->readByte(offset);
    switch (opcode) {
      case 0x21:    // LD HL <data> - loads the instr table offset or the pitch
        hl = programRom->readShort(offset + 1);
        offset += 3;
        break;

      case 0x3E:    // LD A <data> - loads the pan value
        a = programRom->readByte(offset + 1);
        offset += 2;
        break;

      case 0xCD: {
        // CALL <addr> - calls a subroutine
        u16 dest = programRom->readShort(offset + 1);
        if (dest == subOffsets.load_instr) {
          programRom->readBytes(hl, sizeof(konami_vendetta_instr_k053260), &drumInfo.instr);
        } else if (dest == subOffsets.set_pan) {
          drumInfo.pan = a;
        } else if (dest == subOffsets.set_pitch) {
          drumInfo.pitch = hl;
        } else if (dest == subOffsets.note_on) {

        }
        offset += 3;
        break;
      }

      case 0xC3:    // JP - sets the drum ptr to state and we're done
        offset += 3;
        return drumInfo;

      default: {
        L_WARN("Unknown opcode {:02X} in KonamiTMNT2 drum table parse", opcode);
        offset += 1;
        break;
      }
    }
  }
  return drumInfo;
}

void KonamiTMNT2Scanner::scanVendetta(
  MAMERomGroup* programRomGroup,
  MAMERomGroup* sampsRomGroup,
  KonamiTMNT2FormatVer fmtVer,
  const std::string& name
) {
  RawFile* programRom = programRomGroup->file;
  RawFile* samplesRom = sampsRomGroup->file;

  u32 seqTableAddr, instrTableOffsetK053260, sampInfoTableOffset, drumBanksOffset, drumsOffset, instrTableOffsetYM2151;
  programRomGroup->getHexAttribute("seq_table", &seqTableAddr);
  programRomGroup->getHexAttribute("ym2151_instr_table", &instrTableOffsetYM2151);
  programRomGroup->getHexAttribute("k053260_instr_table", &instrTableOffsetK053260);
  programRomGroup->getHexAttribute("k053260_samp_info_table", &sampInfoTableOffset);
  programRomGroup->getHexAttribute("k053260_drum_banks", &drumBanksOffset);
  programRomGroup->getHexAttribute("k053260_drums", &drumsOffset);

  if (!seqTableAddr || !instrTableOffsetYM2151 || !instrTableOffsetK053260 ||
      !sampInfoTableOffset || !drumBanksOffset || !drumsOffset) {
    return;
  }

  u32 defaultTickSkipInterval = 0;
  programRomGroup->getHexAttribute("default_tick_skip_interval", &defaultTickSkipInterval);
  u32 clkb = 0xF9;
  programRomGroup->getHexAttribute("CLKB", &clkb);

  auto seqs = loadSeqTable(
    programRom,
    seqTableAddr,
    fmtVer,
    0,
    clkb & 0xFF,
    name
  );

  std::vector<konami_vendetta_instr_k053260> instrs;
  std::vector<konami_vendetta_sample_info> sampInfos;
  std::vector<konami_vendetta_drum_info> drumInfos;

  std::vector<u32> instrPtrs;

  // First gather the instrument pointers from the instrument table
  u32 minInstrPtr = std::numeric_limits<u32>::max();
  for (int i = instrTableOffsetK053260; i < minInstrPtr; i += 2) {
    u32 instrInfoPtr = programRom->readShort(i);
    minInstrPtr = std::min(minInstrPtr, instrInfoPtr);
    instrPtrs.push_back(instrInfoPtr);
  }

  // Load the instruments
  for (auto instrPtr : instrPtrs) {
    konami_vendetta_instr_k053260 instr;
    programRom->readBytes(instrPtr, sizeof(konami_vendetta_instr_k053260), &instr);
    instrs.emplace_back(instr);
  }

  // Load Drums. Drums end at YM2151 Instr Table
  std::map<u16, int> drumOffsetToIdx;
  vendetta_sub_offsets subOffsets = { 0xA05, 0x2F29, 0xB85, 0xC18 };
  int drumIdx = 0;
  for (u16 i = drumsOffset; i < instrTableOffsetYM2151; ) {
    // track the drum's code offset (+3) to its index as this is how each drum in a bank is referenced
    drumOffsetToIdx[i + 3] = drumIdx++;
    auto drumInfo = parseVendettaDrum(programRom, i, subOffsets);
    drumInfos.emplace_back(drumInfo);
  }
  // for (int i = drumBanksOffset; i < instrTableOffsetYM2151; ) {
  //   u16 drumOffset = programRom->readShort(i);
  //   i += 2;
  //   if (drumOffset == 0)
  //     continue;
  //   auto drumInfo = parseVendettaDrum(programRom, drumOffset, subOffsets);
  //   drumInfos.emplace_back(drumInfo);
  //   i += 0x20;      // each drum bank is a fixed 32 bytes.
  // }

  // Find the last used sample info among all instruments and drums
  u8 lastSampInfoIdx = 0;
  for (auto instr : instrs) {
    lastSampInfoIdx = std::max(lastSampInfoIdx, instr.samp_info_idx);
  }
  for (auto drumInfo : drumInfos) {
    lastSampInfoIdx = std::max(lastSampInfoIdx, drumInfo.instr.samp_info_idx);
  }

  // Load the sample infos
  for (int i = 0; i < lastSampInfoIdx + 1; ++i) {
    int offset = sampInfoTableOffset + (i * sizeof(konami_vendetta_sample_info));
    konami_vendetta_sample_info sampInfo;
    programRom->readBytes(offset, sizeof(konami_vendetta_sample_info), &sampInfo);
    sampInfos.emplace_back(sampInfo);
  }

  std::string instrSetName = fmt::format("{} instrument set", name);
  auto instrSet = new KonamiVendettaSampleInstrSet(
    programRom,
    sampInfoTableOffset,
    instrTableOffsetYM2151,
    instrTableOffsetK053260,
    sampInfoTableOffset,
    drumBanksOffset,
    drumOffsetToIdx,
    instrs,
    sampInfos,
    drumInfos,
    name,
    fmtVer
  );
  if (!instrSet->loadVGMFile()) {
    delete instrSet;
    instrSet = nullptr;
  }

  std::vector<KonamiTMNT2SampColl::sample_info> commonSampInfos;
  for (auto const& sampInfo : sampInfos) {
    commonSampInfos.emplace_back(KonamiTMNT2SampColl::sample_info::makeSampleInfo(sampInfo));
  }
  std::string sampCollName = fmt::format("{} sample collection", name);

  auto sampcoll = new KonamiTMNT2SampColl(
    samplesRom,
    commonSampInfos,
    0,
    static_cast<uint32_t>(samplesRom->size()),
    sampCollName
  );
  if (!sampcoll->loadVGMFile()) {
    delete sampcoll;
    sampcoll = nullptr;
  }

  auto opmInstrSet = new KonamiTMNT2OPMInstrSet(programRom, fmtVer, instrTableOffsetYM2151, "YM2151 Instrument Set");
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