#include "KonamiArcadeDefinitions.h"
#include "KonamiArcadeScanner.h"
#include "KonamiArcadeSeq.h"
#include "KonamiArcadeInstr.h"
#include "VGMColl.h"
#include "MAMELoader.h"
#include "VGMMiscFile.h"


KonamiArcadeFormatVer konamiArcadeVersionEnum(const std::string &versionStr) {
  static const std::unordered_map<std::string, KonamiArcadeFormatVer> versionMap = {
    {"MysticWarrior", MysticWarrior},
    {"GX", GX},
  };

  auto it = versionMap.find(versionStr);
  return it != versionMap.end() ? it->second : VERSION_UNDEFINED;
}

// Mystic Warrior (Z80)
// 208D  ld   a,$71        3E 71
// 208F  ld   ($E227),a    32 27 E2   ; set NMI rate to 0x71
BytePattern KonamiArcadeScanner::ptn_MW_SetNmiRate("\x3E\x71\x32\x27\xE2", "x?xxx", 5);

// Mystic Warrior (Z80)
// 006F  ld   a,$03        3E 03      ; set nmi_skip_mask to 3
// 0071  and  (hl)         A6         ; nmi_counter & nmi_skip_mask
// 0072  jp   nz,$0078     C2 78 00   ; handle NMI if nmi_count & nmi_skip_mask == 0 (in this case every fourth NMI)
// 0075  inc  l            2C
// 0076  ld   (hl),$01     36 01      ; set nmi_skip flag. will enter infinite loop
BytePattern KonamiArcadeScanner::ptn_MW_NmiSkip("\x3E\x03\xA6\xC2\x78\x00\x2C\x36\x01", "x?xxxxxxx", 9);

// Salamander 2 (MC68000)
// 14A0  move.b #0x6d, $20044e.l   13 fc 00 6d 00 20 04 4e    ; set k054539 timer (NMI rate) to 0x6d
BytePattern KonamiArcadeScanner::ptn_GX_SetNmiRate("\x13\xfc\x00\x6d\x00\x20\x04\x4e", "xxx?xxxx", 8);

// Salamander 2 (MC68000)
// 1994  movea.l  #0x67e4, A0      20 7c 00 00 67 e4
// 199A  movea.l  #0x102344, A1    22 7c 00 10 23 44
// 19A0  moveq    #0x1, D2         74 01
// 19A2  moveq    #0x7, D1         72 07
// 19A4  movea.l  (A0)+, A2        24 58
BytePattern KonamiArcadeScanner::ptn_GX_setSeqPlaylistTable("\x20\x7C\x00\x00\x67\xE4\x22\x7C\x00\x10\x23\x44", "xx????xxxxxx", 12);


// Salamander 2 (MC68000)
// 2296  movea.l   #samp_info_set_pointers,A6                 2c 7c 00 00 5f c2
// 229C  movea.l   (0x0,A6,D6.w)=>samp_info_set_pointers,A6.  2c 76 60 00
BytePattern KonamiArcadeScanner::ptn_GX_setSampInfoSetPtrTable("\x2C\x7C\x00\x00\x5F\xC2\x2C\x76\x60\x00", "xx????xxxx", 10);

// Salamander 2 (MC68000)
// move.l  #read_3_params,(offset DAT_0010014e,A0)   21 7c 00 00 39 b2 01 4e  ; set read 3 params callback
// move.l  #DAT_00006584,(offset DAT_001000e2,A0)    21 7c 00 00 65 84 00 e2  ; set drumkit samp info table addr
// move.l  #DAT_0000669b,(offset DAT_001000e6,A0)    21 7c 00 00 66 9b 00 e6  ; set drumkit table addr
BytePattern KonamiArcadeScanner::ptn_GX_setDrumkitPtrs(
  "\x21\x7C\x00\x00\x39\xB2\x01\x4E\x21\x7C\x00\x00\x65\x84\x00\xE2\x21\x7C\x00\x00\x66\x9B\x00\xE6",
  "xx????xxxx????xxxx????xx", 24);


void KonamiArcadeScanner::scan(RawFile *file, void *info) {
  MAMEGame *gameentry = static_cast<MAMEGame*>(info);
  KonamiArcadeFormatVer fmt_ver = konamiArcadeVersionEnum(gameentry->fmt_version_str);

  if (fmt_ver == VERSION_UNDEFINED) {
    L_ERROR("XML entry uses an undefined format version: {}", gameentry->fmt_version_str);
    return;
  }

  MAMERomGroup *seqRomGroupEntry = gameentry->getRomGroupOfType("soundcpu");
  MAMERomGroup *sampsRomGroupEntry = gameentry->getRomGroupOfType("sound");
  if (!seqRomGroupEntry || !sampsRomGroupEntry)
    return;
  u32 seq_table_offset = 0;
  u32 samp_tables_offset = 0;
  u32 drum_samp_table_offset = 0;
  u32 drum_table = 0;
  if (!seqRomGroupEntry->file || !sampsRomGroupEntry->file)
    return;

  seqRomGroupEntry->getHexAttribute("seq_table", &seq_table_offset);
  seqRomGroupEntry->getHexAttribute("samp_tables", &samp_tables_offset);
  seqRomGroupEntry->getHexAttribute("drum_samp_table", &drum_samp_table_offset);
  seqRomGroupEntry->getHexAttribute("drum_table", &drum_table);

  auto codeFile = seqRomGroupEntry->file;
  auto samplesFile = sampsRomGroupEntry->file;

  float nmiRate;
  if (fmt_ver == MysticWarrior) {
    // The NMI skip count determines how many subsequent non-maskable interrupts will be ignored by
    // the driver after processing an NMI. It effectively serves as a divisor for the NMI rate.
    // For example: Mystic Warriors skips 3 NMIs, so only every 4th NMI is handled. For each
    // non-handled NMI, the code enters an infinite loop while waiting for the next NMI.
    u32 nmiSkipCountAddr;
    if (!codeFile->searchBytePattern(ptn_MW_NmiSkip, nmiSkipCountAddr))
      return;
    u8 nmiSkipCount = codeFile->readByte(nmiSkipCountAddr + 1);

    // The NMI rate is the byte sent to K054539 mem addr 0x227 which controls the rate at which the
    // K054539 sends non-maskable interrupt signals to the Z80. It is a key variable in the formula
    // for converting tempo values into real time units.
    u32 setNmiRateAddr;
    if (!codeFile->searchBytePattern(ptn_MW_SetNmiRate, setNmiRateAddr))
      return;
    u8 nmiTimerByte = codeFile->readByte(setNmiRateAddr + 1);
    nmiRate = NMI_TIMER_HERZ(nmiTimerByte, nmiSkipCount);
  } else {    // GX
    // Same explanation as above.
    u32 setNmiRateAddr;
    u8 nmiTimerByte;
    if (codeFile->searchBytePattern(ptn_GX_SetNmiRate, setNmiRateAddr)) {
      nmiTimerByte = codeFile->readByte(setNmiRateAddr + 3);
    } else {
      nmiTimerByte = 109;
    }

    // The skip count is consistently 1. An example of the logic in Salamander 2:
    // 1C0E  cmpi.b  #0x2,$10230a.l  ; loop while IRQ tick counter is less than 2
    // The IRQ tick counter is reset upon execution.
    nmiRate = NMI_TIMER_HERZ(nmiTimerByte, 1);

    if (seq_table_offset == 0) {
      u32 setSeqTableTableAddr;
      if (!codeFile->searchBytePattern(ptn_GX_setSeqPlaylistTable, setSeqTableTableAddr))
        return;
      u32 seq_table_table_offset = codeFile->readWordBE(setSeqTableTableAddr + 2);
      u32 seq_playlist_offset = codeFile->readWordBE(seq_table_table_offset + 4);
      seq_table_offset = codeFile->readWordBE(seq_playlist_offset);
    }

    if (samp_tables_offset == 0) {
      u32 sampInfoSetPtrTableAddr;
      if (!codeFile->searchBytePattern(ptn_GX_setSampInfoSetPtrTable, sampInfoSetPtrTableAddr))
        return;
      samp_tables_offset = codeFile->readWordBE(sampInfoSetPtrTableAddr + 2);
    }

    // Some later games of the format don't use drumkits
    if (drum_samp_table_offset == 0 || drum_table == 0) {
      u32 setDrumKitPtrsAddr;
      if (codeFile->searchBytePattern(ptn_GX_setDrumkitPtrs, setDrumKitPtrsAddr)) {
        drum_samp_table_offset = codeFile->readWordBE(setDrumKitPtrsAddr + 10);
        drum_table = codeFile->readWordBE(setDrumKitPtrsAddr + 18);
      }
    }
  }

  const auto sampInfos = loadSampleInfos(codeFile, samp_tables_offset,
    drum_samp_table_offset, drum_table, fmt_ver);

  std::string instrSetName = fmt::format("{} instrument set", gameentry->name);

  auto instrSet = new KonamiArcadeInstrSet(
    codeFile,
    samp_tables_offset,
    instrSetName,
    drum_table,
    drum_samp_table_offset,
    fmt_ver
  );
  if (!instrSet->loadVGMFile()) {
    delete instrSet;
    instrSet = nullptr;
  }

  std::string sampCollName = fmt::format("{} sample collection", gameentry->name);

  auto sampcoll = new KonamiArcadeSampColl(samplesFile, instrSet, sampInfos, 0,
    static_cast<uint32_t>(samplesFile->size()), sampCollName);
  if (!sampcoll->loadVGMFile()) {
    delete sampcoll;
    sampcoll = nullptr;
  }

  std::vector<KonamiArcadeSeq*> seqs = loadSeqTable(
    seqRomGroupEntry->file,
    seq_table_offset,
    instrSet->drums(),
    nmiRate,
    gameentry->name,
    fmt_ver
  );

  for (auto seq : seqs) {
    VGMColl* coll = new VGMColl(seq->name());

    coll->useSeq(seq);
    coll->addInstrSet(instrSet);
    coll->addSampColl(sampcoll);
    if (!coll->load()) {
      delete coll;
    }
  }
}

struct sequence_table_entry {
  u8 unknown[7];
  u8 bank;
  u8 memDestinationLow;
  u8 memDestinationHigh;
  u8 unknown2[4];
};

struct sequence_table_entry_gx {
  u8 unknown[8];
  u32 seqPointer;
};

const std::vector<KonamiArcadeSeq*> KonamiArcadeScanner::loadSeqTable(
  RawFile *file,
  u32 offset,
  const std::array<KonamiArcadeInstrSet::drum, 46>& drums,
  float nmiRate,
  std::string gameName,
  KonamiArcadeFormatVer fmtVer
) {
  auto seqTableName = fmt::format("{} sequence pointer table", gameName);
  VGMMiscFile *seqTable = new VGMMiscFile(KonamiArcadeFormat::name, file, offset, 1, seqTableName);
  // Add SeqTable as Miscfile
  if (!seqTable->loadVGMFile()) {
    delete seqTable;
    return {};
  }
  std::vector<KonamiArcadeSeq*> seqs;
  uint32_t nFileLength = static_cast<uint32_t>(file->size());

  int seqCounter = 0;
  if (fmtVer == GX) {
    int entryLength = 12;
    u32 testLong = file->readWordBE(offset + 12);
    if (testLong > 0x1000 && testLong < 0x20000)
      entryLength = 16;

    while (offset < nFileLength) {
      u32 unknown = file->readWordBE(offset);
      if (unknown >= 0x5000)
        break;
      u32 seqPointer = file->readWordBE(offset + 8);
      if (seqPointer == 0 || seqPointer >= nFileLength)
        break;
      auto name = fmt::format("{} {:d}", gameName, seqCounter++);
      KonamiArcadeSeq *newSeq = new KonamiArcadeSeq(file, GX, seqPointer, 0, drums, nmiRate, name);
      if (!newSeq->loadVGMFile())
        delete newSeq;
      else
        seqs.push_back(newSeq);

      auto child = seqTable->addChild(offset, sizeof(sequence_table_entry_gx), "Sequence Info Block");
      child->addUnknownChild(offset, 5);
      child->addChild(offset + 5, 1, "Tempo Related");
      child->addUnknownChild(offset + 6, 2);
      child->addChild(offset + 8, 4, "Sequence Pointer");

      offset += entryLength;
    }
  } else { // MysticWarrior
    while (offset < nFileLength) {
      if (file->readShort(offset) != 0x0000) {
        break;
      }
      sequence_table_entry entry;
      file->readBytes(offset, sizeof(sequence_table_entry), &entry);
      u16 dest = (entry.memDestinationHigh << 8) | entry.memDestinationLow;
      u32 seqOffset = (entry.bank * 0x400) + (dest - 0x8000);
      if (seqOffset == 0 || seqOffset >= nFileLength)
        break;
      auto name = fmt::format("{} {:d}", gameName, seqCounter++);
      KonamiArcadeSeq *newSeq = new KonamiArcadeSeq(file, MysticWarrior, seqOffset, dest, drums, nmiRate, name);
      if (!newSeq->loadVGMFile())
        delete newSeq;
      else
        seqs.push_back(newSeq);

      auto child = seqTable->addChild(offset, sizeof(sequence_table_entry), "Sequence Pointer");
      child->addUnknownChild(offset, 7);
      child->addChild(offset + 7, 1, "Bank");
      child->addChild(offset + 8, 2, "Memory Destination");
      child->addUnknownChild(offset + 10, 4);

      offset += sizeof(sequence_table_entry);
    }
  }

  seqTable->setLength(offset - seqTable->startOffset());

  return seqs;
}

const std::vector<konami_mw_sample_info> KonamiArcadeScanner::loadSampleInfos(
  RawFile *file,
  u32 tablesOffset,
  u32 drumSampTableOffset,
  u32 drumInstrTableOffset,
  KonamiArcadeFormatVer fmtVer
) {
  std::vector<konami_mw_sample_info> sampInfos;
  u32 instrSampleTableOffset = fmtVer == MysticWarrior ? file->readShort(tablesOffset) : file->readWordBE(tablesOffset);
  u32 sfxSampleTableOffset = fmtVer == MysticWarrior ? file->readShort(tablesOffset+2) : file->readWordBE(tablesOffset+4);

  for (u32 off = instrSampleTableOffset; off < sfxSampleTableOffset; off += sizeof(konami_mw_sample_info)) {
    konami_mw_sample_info info;
    file->readBytes(off, sizeof(konami_mw_sample_info), &info);
    sampInfos.push_back(info);
  }
  if (drumSampTableOffset != 0) {
    for (u32 off = drumSampTableOffset; off < drumInstrTableOffset; off += sizeof(konami_mw_sample_info)) {
      konami_mw_sample_info info;
      file->readBytes(off, sizeof(konami_mw_sample_info), &info);
      sampInfos.push_back(info);
    }
  }
  return sampInfos;
}
