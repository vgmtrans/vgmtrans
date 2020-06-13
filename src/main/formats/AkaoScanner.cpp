#include "pch.h"
#include "AkaoScanner.h"
#include "AkaoSeq.h"
#include "AkaoInstr.h"

AkaoScanner::AkaoScanner(void) {
}

AkaoScanner::~AkaoScanner(void) {
}

void AkaoScanner::Scan(RawFile *file, void *info) {
  for (uint32_t offset = 0; offset + 0x60 < file->size(); offset++) {
    //sig must match ascii characters "AKAO"
    if (file->GetWordBE(offset) != 0x414B414F)
      continue;

    const uint16_t seq_length = file->GetShort(offset + 6);
    if (seq_length != 0) {
      // Sequence
      if (!AkaoSeq::IsPossibleAkaoSeq(file, offset))
        continue;

      const AkaoPs1Version version = AkaoSeq::GuessVersion(file, offset);
      AkaoSeq *NewAkaoSeq = new AkaoSeq(file, offset, version);
      if (!NewAkaoSeq->LoadVGMFile())
        delete NewAkaoSeq;
    }
    else {
      // Samples
      if (file->GetWord(offset + 8) != 0 || file->GetWord(offset + 0x0C) != 0 ||
        file->GetWord(offset + 0x24) != 0 || file->GetWord(offset + 0x28) != 0 || file->GetWord(offset + 0x2C) != 0 &&
        file->GetWord(offset + 0x30) != 0 || file->GetWord(offset + 0x34) != 0 || file->GetWord(offset + 0x38) != 0 &&
        file->GetWord(offset + 0x3C) != 0 ||
        file->GetWord(offset + 0x40) != 0 ||
        file->GetWord(offset + 0x4C) == 0)        //ADSR1 and ADSR2 will never be 0 in any real-world case.
                                             //file->GetWord(i+0x50) == 0 || file->GetWord(i+0x54) == 00) //Chrono Cross 311 is exception to this :-/
        continue;

      AkaoSampColl *sampColl = new AkaoSampColl(file, offset, 0);
      if (!sampColl->LoadVGMFile())
        delete sampColl;
    }
  }
}
