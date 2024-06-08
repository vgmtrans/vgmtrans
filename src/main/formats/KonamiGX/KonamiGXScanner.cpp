#include "KonamiGXScanner.h"
#include "KonamiGXSeq.h"
#include "MAMELoader.h"

void KonamiGXScanner::scan(RawFile *file, void *info) {
  MAMEGame *gameentry = (MAMEGame *) info;
  MAMERomGroup *seqRomGroupEntry = gameentry->getRomGroupOfType("soundcpu");
  MAMERomGroup *sampsRomGroupEntry = gameentry->getRomGroupOfType("shared");
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


void KonamiGXScanner::loadSeqTable(RawFile *file, uint32_t offset) {
  uint32_t nFileLength = static_cast<uint32_t>(file->size());
  while (offset < nFileLength) {
    uint32_t seqOffset = file->readWordBE(offset);
    if (seqOffset == 0 || seqOffset >= nFileLength)
      break;
    KonamiGXSeq *newSeq = new KonamiGXSeq(file, seqOffset);
    if (!newSeq->loadVGMFile())
      delete newSeq;
    offset += 12;
  }
}
