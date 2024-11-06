#include "KonamiArcadeScanner.h"
#include "KonamiArcadeSeq.h"
#include "KonamiArcadeInstr.h"
#include "VGMColl.h"
#include "MAMELoader.h"

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

  auto seqs = loadSeqTable(seqRomGroupEntry->file, seq_table_offset);

  auto codeFile = seqRomGroupEntry->file;
  auto samplesFile = sampsRomGroupEntry->file;

  auto sampInfos = loadSampleInfos(codeFile, samp_tables_offset,
    drum_samp_table_offset, drum_table);

  std::string instrSetName = fmt::format("{} instrument set", gameentry->name);

  auto instrSet = new KonamiArcadeInstrSet(codeFile, samp_tables_offset, instrSetName);
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

struct SequenceTableEntry {
  u8 unknown[7];
  u8 bank;
  u8 memDestinationLow;
  u8 memDestinationHigh;
  u8 unknown2[4];
};

std::vector<KonamiArcadeSeq*> KonamiArcadeScanner::loadSeqTable(RawFile *file, uint32_t offset) {
  std::vector<KonamiArcadeSeq*> seqs;
  uint32_t nFileLength = static_cast<uint32_t>(file->size());
  while (offset < nFileLength) {
    if (file->readShort(offset) == 0xFFFF) {
      break;
    }
    SequenceTableEntry entry;
    file->readBytes(offset, sizeof(SequenceTableEntry), &entry);
    u16 dest = entry.memDestinationHigh << 8 | entry.memDestinationLow;
    u32 seqOffset = (entry.bank * 0x400) + (dest - 0x8000);
    if (seqOffset == 0 || seqOffset >= nFileLength)
      break;
    KonamiArcadeSeq *newSeq = new KonamiArcadeSeq(file, seqOffset, dest);
    if (!newSeq->loadVGMFile())
      delete newSeq;
    else
      seqs.push_back(newSeq);
    offset += sizeof(SequenceTableEntry);
  }
  return seqs;
}

std::vector<konami_mw_sample_info> KonamiArcadeScanner::loadSampleInfos(
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