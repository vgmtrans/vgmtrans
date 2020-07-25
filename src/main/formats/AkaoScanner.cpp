#include "pch.h"
#include "AkaoScanner.h"
#include "AkaoSeq.h"
#include "AkaoInstr.h"

AkaoScanner::AkaoScanner(void) {
}

AkaoScanner::~AkaoScanner(void) {
}

void AkaoScanner::Scan(RawFile *file, void *info) {
  AkaoPs1Version version = DetermineVersionFromTag(file);

  for (uint32_t offset = 0; offset + 0x60 < file->size(); offset++) {
    //sig must match ascii characters "AKAO"
    if (file->GetWordBE(offset) != 0x414B414F)
      continue;

    const uint16_t seq_length = file->GetShort(offset + 6);
    if (seq_length != 0) {
      // Sequence
      if (!AkaoSeq::IsPossibleAkaoSeq(file, offset))
        continue;

      if (version == AkaoPs1Version::UNKNOWN)
        version = AkaoSeq::GuessVersion(file, offset);
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

      if (version == AkaoPs1Version::VERSION_3_0)
        continue; // not implemented yet

      AkaoSampColl *sampColl = new AkaoSampColl(file, offset, 0);
      if (!sampColl->LoadVGMFile())
        delete sampColl;
    }
  }
}

AkaoPs1Version AkaoScanner::DetermineVersionFromTag(RawFile *file) {
  const std::wstring & album = file->tag.album;
  if (album == L"Final Fantasy 7" || album == L"Final Fantasy VII")
    return AkaoPs1Version::VERSION_1_0;
  else if (album == L"SaGa Frontier")
    return AkaoPs1Version::VERSION_1_1;
  else if (album == L"Front Mission 2" || album == L"Chocobo's Mysterious Dungeon")
    return AkaoPs1Version::VERSION_1_2;
  else if (album == L"Parasite Eve")
    return AkaoPs1Version::VERSION_2;
  else if (album == L"Another Mind")
    return AkaoPs1Version::VERSION_3_0;
  else if (album == L"Chocobo Dungeon 2" || album == L"Final Fantasy 8" || album == L"Final Fantasy VIII"
    || album == L"Chocobo Racing" || album == L"SaGa Frontier 2" || album == L"Racing Lagoon")
    return AkaoPs1Version::VERSION_3_1;
  else if (album == L"Legend of Mana" || album == L"Front Mission 3" || album == L"Chrono Cross"
    || album == L"Vagrant Story" || album == L"Final Fantasy 9" || album == L"Final Fantasy IX"
    || album == L"Final Fantasy Origins - FF2")
    return AkaoPs1Version::VERSION_3_2;
  return AkaoPs1Version::UNKNOWN;
}
