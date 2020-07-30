#include "pch.h"
#include "AkaoScanner.h"
#include "AkaoSeq.h"
#include "AkaoInstr.h"

AkaoScanner::AkaoScanner(void) {
}

AkaoScanner::~AkaoScanner(void) {
}

void AkaoScanner::Scan(RawFile *file, void *info) {
  const AkaoPs1Version file_version = DetermineVersionFromTag(file);

  for (uint32_t offset = 0; offset + 0x60 < file->size(); offset++) {
    //sig must match ascii characters "AKAO"
    if (file->GetWordBE(offset) != 0x414B414F)
      continue;

    const uint16_t seq_length = file->GetShort(offset + 6);
    if (seq_length != 0) {
      // Sequence
      if (!AkaoSeq::IsPossibleAkaoSeq(file, offset))
        continue;

      AkaoPs1Version version = file_version;
      if (version == AkaoPs1Version::UNKNOWN)
        version = AkaoSeq::GuessVersion(file, offset);
      AkaoSeq *seq = new AkaoSeq(file, offset, version);
      if (!seq->LoadVGMFile()) {
        delete seq;
        continue;
      }

      AkaoInstrSet* instrset = seq->NewInstrSet();
      if (instrset == nullptr)
        continue;
      if (!instrset->LoadVGMFile())
        delete instrset;
    }
    else {
      // Samples
      if (!AkaoSampColl::IsPossibleAkaoSampColl(file, offset))
        continue;

      AkaoPs1Version version = file_version;
      if (version == AkaoPs1Version::UNKNOWN)
        version = AkaoSampColl::GuessVersion(file, offset);

      AkaoSampColl *sampColl = new AkaoSampColl(file, offset, version);
      if (!sampColl->LoadVGMFile())
        delete sampColl;
    }
  }

  if (file->size() >= 0x1A8000) {
    // Hard-coded loader for Final Fantasy 7 PSF
    const AkaoInstrDatLocation instrLocation(0xf0000, 0x166000, 0, 128);
    const AkaoInstrDatLocation instr2Location(0x168000, 0x1a6000, 53, 75); // One-Winged Angel

    std::vector<AkaoInstrDatLocation> instrLocations;

    if (file->GetWord(instrLocation.instrAllOffset) == 0x1010
      && file->GetWord(instrLocation.instrDatOffset) == 0x1010)
      instrLocations.push_back(instrLocation);

    //if (file->GetWord(instr2Location.instrAllOffset) == 0x38560
    //  && file->GetWord(instr2Location.instrDatOffset) == 0x38560)
    //  instrLocations.push_back(instr2Location);

    if (!instrLocations.empty()) {
      for (const auto & loc : instrLocations) {
        AkaoSampColl *sampColl = new AkaoSampColl(file, loc);
        if (!sampColl->LoadVGMFile())
          delete sampColl;
      }
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
