#include "KonamiArcadeDefinitions.h"
#include "KonamiArcadeScanner.h"
#include "KonamiArcadeSeq.h"
#include "KonamiArcadeInstr.h"
#include "VGMColl.h"
#include "MAMELoader.h"

// Mystic Warrior
// 208D  ld   a,$71        3E 71
// 208F  ld   ($E227),a    32 27 E2   ; set NMI rate to 0x71

BytePattern KonamiArcadeScanner::ptnSetNmiRate("\x3E\x71\x32\x27\xE2", "x?xxx", 5);

// Mystic Warrior
// 006F  ld   a,$03        3E 03      ; set nmi_skip_mask to 3
// 0071  and  (hl)         A6         ; nmi_counter & nmi_skip_mask
// 0072  jp   nz,$0078     C2 78 00   ; handle NMI if nmi_count & nmi_skip_mask == 0 (in this case every fourth NMI)
// 0075  inc  l            2C
// 0076  ld   (hl),$01     36 01      ; set nmi_skip flag. will enter infinite loop

BytePattern KonamiArcadeScanner::ptnNmiSkip("\x3E\x03\xA6\xC2\x78\x00\x2C\x36\x01", "x?xxxxxxx", 9);

void KonamiArcadeScanner::scan(RawFile *file, void *info) {
  MAMEGame *gameentry = (MAMEGame *) info;
  MAMERomGroup *seqRomGroupEntry = gameentry->getRomGroupOfType("soundcpu");
  MAMERomGroup *sampsRomGroupEntry = gameentry->getRomGroupOfType("sound");
  if (!seqRomGroupEntry || !sampsRomGroupEntry)
    return;
  u32 seq_table_offset;
  u32 samp_tables_offset;
  u32 drum_samp_table_offset;
  u32 drum_table;
  if (!seqRomGroupEntry->file || !sampsRomGroupEntry->file ||
      !seqRomGroupEntry->getHexAttribute("seq_table", &seq_table_offset) ||
      !seqRomGroupEntry->getHexAttribute("samp_tables", &samp_tables_offset) ||
      !seqRomGroupEntry->getHexAttribute("drum_samp_table", &drum_samp_table_offset) ||
      !seqRomGroupEntry->getHexAttribute("drum_table", &drum_table))
    return;

  auto codeFile = seqRomGroupEntry->file;
  auto samplesFile = sampsRomGroupEntry->file;

  // The NMI skip count determines how many subsequent non-maskable interrupts will be ignored by
  // the driver after processing an NMI. It effectively serves as a divisor for the NMI rate.
  // For example: Mystic Warriors skips 3 NMIs, so only every 4th NMI is handled. For each
  // non-handled NMI, the code enters an infinite loop while waiting for the next NMI.
  u32 nmiSkipCountAddr;
  if (!codeFile->searchBytePattern(ptnNmiSkip, nmiSkipCountAddr))
    return;
  u8 nmiSkipCount = codeFile->readByte(nmiSkipCountAddr+1);

  // The NMI rate is the byte sent to K054539 mem addr 0x227 which controls the rate at which the
  // K054539 sends non-maskable interrupt signals to the Z80. It is a key variable in the formula
  // for converting tempo values into real time units.
  u32 setNmiRateAddr;
  if (!codeFile->searchBytePattern(ptnSetNmiRate, setNmiRateAddr))
    return;
  u8 nmiTimerByte = codeFile->readByte(setNmiRateAddr+1);
  float nmiRate = NMI_TIMER_HERZ(nmiTimerByte, nmiSkipCount);

  const auto sampInfos = loadSampleInfos(codeFile, samp_tables_offset,
    drum_samp_table_offset, drum_table);

  std::string instrSetName = fmt::format("{} instrument set", gameentry->name);

  auto instrSet = new KonamiArcadeInstrSet(codeFile, samp_tables_offset, instrSetName, drum_table);
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

  auto seqs = loadSeqTable(
    seqRomGroupEntry->file,
    seq_table_offset,
    instrSet->drums(),
    nmiRate
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

const std::vector<KonamiArcadeSeq*> KonamiArcadeScanner::loadSeqTable(
  RawFile *file,
  uint32_t offset,
  const std::array<KonamiArcadeInstrSet::drum, 46>& drums,
  float nmiRate
) {
  std::vector<KonamiArcadeSeq*> seqs;
  uint32_t nFileLength = static_cast<uint32_t>(file->size());
  while (offset < nFileLength) {
    if (file->readShort(offset) != 0x0000) {
      break;
    }
    sequence_table_entry entry;
    file->readBytes(offset, sizeof(sequence_table_entry), &entry);
    u16 dest = entry.memDestinationHigh << 8 | entry.memDestinationLow;
    u32 seqOffset = (entry.bank * 0x400) + (dest - 0x8000);
    if (seqOffset == 0 || seqOffset >= nFileLength)
      break;
    KonamiArcadeSeq *newSeq = new KonamiArcadeSeq(file, seqOffset, dest, drums, nmiRate);
    if (!newSeq->loadVGMFile())
      delete newSeq;
    else
      seqs.push_back(newSeq);
    offset += sizeof(sequence_table_entry);
  }
  return seqs;
}

const std::vector<konami_mw_sample_info> KonamiArcadeScanner::loadSampleInfos(
  RawFile *file,
  u32 tablesOffset,
  u32 drumSampTableOffset,
  u32 drumInstrTableOffset
) {
  std::vector<konami_mw_sample_info> sampInfos;
  u32 instrSampleTableOffset = file->readShort(tablesOffset);
  u32 sfxSampleTableOffset = file->readShort(tablesOffset+2);

  for (u32 off = instrSampleTableOffset; off < sfxSampleTableOffset; off += sizeof(konami_mw_sample_info)) {
    konami_mw_sample_info info;
    file->readBytes(off, sizeof(konami_mw_sample_info), &info);
    sampInfos.push_back(info);
  }
  for (u32 off = drumSampTableOffset; off < drumInstrTableOffset; off += sizeof(konami_mw_sample_info)) {
    konami_mw_sample_info info;
    file->readBytes(off, sizeof(konami_mw_sample_info), &info);
    sampInfos.push_back(info);
  }
  return sampInfos;
}