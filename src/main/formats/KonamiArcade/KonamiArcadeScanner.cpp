#include "KonamiArcadeScanner.h"
#include "KonamiArcadeSeq.h"
#include "MAMELoader.h"

void KonamiArcadeScanner::scan(RawFile *file, void *info) {
  MAMEGame *gameentry = (MAMEGame *) info;
  MAMERomGroup *seqRomGroupEntry = gameentry->getRomGroupOfType("soundcpu");
  MAMERomGroup *sampsRomGroupEntry = gameentry->getRomGroupOfType("sound");
  if (!seqRomGroupEntry || !sampsRomGroupEntry)
    return;
  uint32_t seq_table_offset;
  //uint32_t instr_table_offset;
  //uint32_t samp_table_offset;
  if (!seqRomGroupEntry->file || !sampsRomGroupEntry->file ||
      !seqRomGroupEntry->getHexAttribute("seq_table", &seq_table_offset))// ||
        //!seqRomGroupEntry->GetHexAttribute("samp_table", &samp_table_offset))
          return;

  loadSeqTable(seqRomGroupEntry->file, seq_table_offset);
}

struct SequenceTableEntry {
  u8 unknown[7];
  u8 bank;
  u8 memDestinationLow;
  u8 memDestinationHigh;
  u8 unknown2[4];
};

void KonamiArcadeScanner::loadSeqTable(RawFile *file, uint32_t offset) {
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
    offset += sizeof(SequenceTableEntry);
  }
}
