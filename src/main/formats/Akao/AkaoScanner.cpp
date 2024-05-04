/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoSeq.h"
#include "AkaoInstr.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<AkaoScanner> s_akao("AKAO");
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

    // Add choir samples for One-Winged Angel.
    // It is unlikely that any other song will load this sample collection in actual gameplay.
    if (file->GetWord(instr2Location.instrAllOffset) == 0x38560 &&
        file->GetWord(instr2Location.instrDatOffset) == 0x38560)
      instrLocations.push_back(instr2Location);

    if (!instrLocations.empty()) {
      for (const auto & loc : instrLocations) {
        auto *sampColl = new AkaoSampColl(file, loc);
        if (!sampColl->LoadVGMFile())
          delete sampColl;
      }
    }
  }
}

AkaoPs1Version AkaoScanner::DetermineVersionFromTag(RawFile *file) noexcept {
  const std::string & album = file->tag.album;
  if (album == "Final Fantasy 7" || album == "Final Fantasy VII")
    return AkaoPs1Version::VERSION_1_0;
  else if (album == "SaGa Frontier")
    return AkaoPs1Version::VERSION_1_1;
  else if (album == "Front Mission 2" || album == "Chocobo's Mysterious Dungeon")
    return AkaoPs1Version::VERSION_1_2;
  else if (album == "Parasite Eve")
    return AkaoPs1Version::VERSION_2;
  else if (album == "Another Mind")
    return AkaoPs1Version::VERSION_3_0;
  else if (album == "Chocobo Dungeon 2" || album == "Final Fantasy 8" || album == "Final Fantasy VIII"
    || album == "Chocobo Racing" || album == "SaGa Frontier 2" || album == "Racing Lagoon")
    return AkaoPs1Version::VERSION_3_1;
  else if (album == "Legend of Mana" || album == "Front Mission 3" || album == "Chrono Cross"
    || album == "Vagrant Story" || album == "Final Fantasy 9" || album == "Final Fantasy IX"
    || album == "Final Fantasy Origins - FF2")
    return AkaoPs1Version::VERSION_3_2;
  return AkaoPs1Version::UNKNOWN;
}
