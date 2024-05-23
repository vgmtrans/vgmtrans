/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoInstr.h"
#include "VGMSamp.h"
#include "PSXSPU.h"

// ************
// AkaoInstrSet
// ************

AkaoInstrSet::AkaoInstrSet(RawFile *file,
                           uint32_t length,
                           AkaoPs1Version version,
                           uint32_t instrOff,
                           uint32_t dkitOff,
                           uint32_t theID,
                           std::string name)
    : VGMInstrSet(AkaoFormat::name, file, 0, length, std::move(name)), version_(version) {
  id = theID;
  instrSetOff = instrOff;
  drumkitOff = dkitOff;
  bMelInstrs = instrSetOff > 0;
  bDrumKit = drumkitOff > 0;
  if (bMelInstrs)
    dwOffset = instrSetOff;
  else
    dwOffset = drumkitOff;
  end_boundary_offset = dwOffset + unLength;
}

AkaoInstrSet::AkaoInstrSet(RawFile *file, uint32_t end_boundary_offset,
  AkaoPs1Version version, const std::set<uint32_t>& custom_instrument_addresses,
  const std::set<uint32_t>& drum_instrument_addresses, std::string name)
  : VGMInstrSet(AkaoFormat::name, file, 0, 0, std::move(name)), bMelInstrs(false), bDrumKit(false),
  instrSetOff(0), drumkitOff(0), end_boundary_offset(end_boundary_offset), version_(version)
{
  uint32_t first_instrument_offset = 0;
  if (!custom_instrument_addresses.empty()) {
    first_instrument_offset = *custom_instrument_addresses.begin();
    dwOffset = first_instrument_offset;
  }

  if (!drum_instrument_addresses.empty()) {
    const uint32_t first_drum_offset = *drum_instrument_addresses.begin();

    if (custom_instrument_addresses.empty())
      dwOffset = first_drum_offset;
    else
      dwOffset = std::min(first_instrument_offset, first_drum_offset);
  }

  this->custom_instrument_addresses = custom_instrument_addresses;
  this->drum_instrument_addresses = drum_instrument_addresses;
}

AkaoInstrSet::AkaoInstrSet(RawFile *file, uint32_t offset,
  uint32_t end_boundary_offset, AkaoPs1Version version, std::string name)
    : VGMInstrSet(AkaoFormat::name, file, offset, 0, std::move(name)), bMelInstrs(false),
      bDrumKit(false), instrSetOff(0), drumkitOff(0), end_boundary_offset(end_boundary_offset),
      version_(version)
{}

bool AkaoInstrSet::GetInstrPointers() {
  if (bMelInstrs) {
    VGMHeader *SSEQHdr = AddHeader(instrSetOff, 0x10, "Instr Ptr Table");
    int i = 0;
    //-1 aka 0xFFFF if signed or 0 and past the first pointer value
    for (int j = instrSetOff; (GetShort(j) != static_cast<uint16_t>(-1)) && ((GetShort(j) != 0) || i == 0) && i < 16; j += 2) {
      SSEQHdr->AddSimpleItem(j, 2, "Instr Pointer");
      aInstrs.push_back(new AkaoInstr(this, instrSetOff + 0x20 + GetShort(j), 0, 1, i++));
    }
  }
  else if (!custom_instrument_addresses.empty()) {
    uint32_t instrNum = 0;
    for (const uint32_t instrOff : custom_instrument_addresses) {
      aInstrs.push_back(new AkaoInstr(this, instrOff, 0, 1, instrNum++));
    }
  }

  if (bDrumKit) {
    aInstrs.push_back(new AkaoDrumKit(this, drumkitOff, 0, 127, 127));
  }
  else if (!drum_instrument_addresses.empty()) {
    uint32_t instrNum = 127;
    for (const uint32_t instrOff : drum_instrument_addresses) {
      aInstrs.push_back(new AkaoDrumKit(this, instrOff, 0, 127, instrNum--));
    }
  }

  return true;
}

// *********
// AkaoInstr
// *********

AkaoInstr::AkaoInstr(AkaoInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                     uint32_t theInstrNum, std::string name)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, std::move(name)), bDrumKit(false) {
}

bool AkaoInstr::LoadInstr() {
  for (int k = 0; dwOffset + k * 8 < GetRawFile()->size(); k++) {
    if (version() < AkaoPs1Version::VERSION_3_0) {
      if (GetByte(dwOffset + k * 8) >= 0x80) {
        AddSimpleItem(dwOffset + k * 8, 8, "Region Terminator");
        break;
      }
    }
    else {
      if (GetByte(dwOffset + k * 8 + 5) == 0) {
        AddSimpleItem(dwOffset + k * 8, 8, "Region Terminator");
        break;
      }
    }

    auto rgn = new AkaoRgn(this, dwOffset + k * 8, 8);
    if (!rgn->LoadRgn()) {
      delete rgn;
      return false;
    }
    aRgns.push_back(rgn);
  }
  SetGuessedLength();

  if (aRgns.empty())
    L_WARN("Instrument has no regions.");

  return true;
}

// ***********
// AkaoDrumKit
// ***********

AkaoDrumKit::AkaoDrumKit(AkaoInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                         uint32_t theInstrNum)
    : AkaoInstr(instrSet, offset, length, theBank, theInstrNum, "Drum Kit") {
  bDrumKit = true;
}

bool AkaoDrumKit::LoadInstr() {
  if (version() >= AkaoPs1Version::VERSION_3_0) {
    for (uint8_t drum_note_number = 0; drum_note_number < 128; drum_note_number++) {
      constexpr uint32_t kRgnLength = 8;
      const uint32_t rgn_offset = dwOffset + drum_note_number * kRgnLength;
      if (rgn_offset + kRgnLength > instrSet()->end_boundary_offset)
        break;

      // skip the region if zero-filled
      if (GetWord(rgn_offset) == 0 && GetWord(rgn_offset + 4) == 0)
        continue;

      // if we run into 0xFFFFFFFF FFFFFFFF, then we quit reading for drumkit regions early
      //
      // Is this necessary? Is there any real-world case? (loveemu)
      if (GetWord(rgn_offset) == 0xFFFFFFFF && GetWord(rgn_offset + 4) == 0xFFFFFFFF)
        break;

      const uint8_t assoc_art_id = GetByte(rgn_offset + 0);
      auto *rgn = new AkaoRgn(this, rgn_offset, kRgnLength, drum_note_number, drum_note_number, assoc_art_id);
      AddRgn(rgn);
      rgn->drumRelUnityKey = GetByte(rgn_offset + 1);
      const uint8_t raw_volume = GetByte(rgn_offset + 6);
      const double volume = raw_volume == 0 ? 1.0 : raw_volume / 128.0;
      rgn->SetVolume(volume);
      rgn->AddGeneralItem(rgn_offset, 1, "Associated Articulation ID");
      rgn->AddGeneralItem(rgn_offset + 1, 1, "Relative Unity Key");
      // TODO: set ADSR to the region
      rgn->AddGeneralItem(rgn_offset + 2, 1, "ADSR Attack Rate");
      rgn->AddGeneralItem(rgn_offset + 3, 1, "ADSR Sustain Rate");
      rgn->AddGeneralItem(rgn_offset + 4, 1, "ADSR Sustain Mode");
      rgn->AddGeneralItem(rgn_offset + 5, 1, "ADSR Release Rate");
      rgn->AddGeneralItem(rgn_offset + 6, 1, "Attenuation");
      const uint8_t raw_pan_reverb = GetByte(rgn_offset + 7);
      const uint8_t pan = raw_pan_reverb & 0x7f;
      const bool reverb = (raw_pan_reverb & 0x80) != 0;
      rgn->AddGeneralItem(rgn_offset + 7, 1, "Pan & Reverb On/Off");
      rgn->SetPan(pan);
      // TODO: set reverb on/off to the region
    }
  }
  else if (version() >= AkaoPs1Version::VERSION_1_0) {
    const uint32_t kRgnLength = version() >= AkaoPs1Version::VERSION_2 ? 6 : 5;
    for (uint8_t drum_key = 0; drum_key < 12; drum_key++) {
      constexpr uint8_t drum_octave = 2; // a drum note ignores octave, this is the octave number for midi remapping
      const uint32_t rgn_offset = dwOffset + drum_key * kRgnLength;
      if (rgn_offset + kRgnLength > instrSet()->end_boundary_offset)
        break;

      // skip the region if zero-filled
      if (GetWord(rgn_offset) == 0 && GetByte(rgn_offset + 4) == 0
        && (version() < AkaoPs1Version::VERSION_2 || GetByte(rgn_offset + 5) == 0))
        continue;

      const uint8_t assoc_art_id = GetByte(rgn_offset + 0);
      const uint8_t drum_note_number = drum_octave * 12 + drum_key;
      auto *rgn = new AkaoRgn(this, rgn_offset, kRgnLength, drum_note_number, drum_note_number, assoc_art_id);
      AddRgn(rgn);
      rgn->drumRelUnityKey = GetByte(rgn_offset + 1);
      const uint16_t raw_volume = GetWord(rgn_offset + 2);
      rgn->SetVolume(raw_volume / static_cast<double>(127 * 128));
      rgn->AddGeneralItem(rgn_offset, 1, "Associated Articulation ID");
      rgn->AddGeneralItem(rgn_offset + 1, 1, "Relative Unity Key");
      rgn->AddGeneralItem(rgn_offset + 2, 2, "Attenuation");
      const uint8_t pan = GetByte(rgn_offset + 4);
      rgn->AddPan(pan, rgn_offset + 4, 1);

      // TODO: set reverb on/off to the region
      if (version() >= AkaoPs1Version::VERSION_2) {
        rgn->AddGeneralItem(rgn_offset + 5, 1, "Reverb On/Off");
      }
    }
  }
  else
    return false;

  SetGuessedLength();

  if (aRgns.empty())
    L_WARN("Instrument has no regions.");

  return true;
}


// *******
// AkaoRgn
// *******
AkaoRgn::AkaoRgn(VGMInstr *instr, uint32_t offset, uint32_t length, uint8_t keyLow, uint8_t keyHigh,
                 uint8_t artIDNum, std::string name)
    : VGMRgn(instr, offset, length, keyLow, keyHigh, 0, 0x7F, 0, std::move(name)),
      adsr1(0), adsr2(0), artNum(artIDNum), drumRelUnityKey(0) {
}

AkaoRgn::AkaoRgn(VGMInstr *instr, uint32_t offset, uint32_t length, std::string name)
    : VGMRgn(instr, offset, length, std::move(name)), adsr1(0), adsr2(0), artNum(0), drumRelUnityKey(0) {
}

bool AkaoRgn::LoadRgn() {
  AddGeneralItem(dwOffset + 0, 1, "Associated Articulation ID");
  artNum = GetByte(dwOffset + 0); //- first_sample_id;
  AddKeyLow(GetByte(dwOffset + 1), dwOffset + 1);
  AddKeyHigh(GetByte(dwOffset + 2), dwOffset + 2);
  // TODO: ADSR conversion?
  AddGeneralItem(dwOffset + 3, 1, "ADSR Attack Rate");
  AddGeneralItem(dwOffset + 4, 1, "ADSR Sustain Rate");
  AddGeneralItem(dwOffset + 5, 1, "ADSR Sustain Mode");
  AddGeneralItem(dwOffset + 6, 1, "ADSR Release Rate");
  const uint8_t raw_volume = GetByte(dwOffset + 7);
  const double volume = raw_volume == 0 ? 1.0 : raw_volume / 128.0;
  AddVolume(volume, dwOffset + 7, 1);

  return true;
}

// ************
// AkaoSampColl
// ************

AkaoSampColl::AkaoSampColl(RawFile *file, uint32_t offset, AkaoPs1Version version, std::string name)
    : VGMSampColl(AkaoFormat::name, file, offset, 0, std::move(name)), starting_art_id(0), sample_set_id(0),
      version_(version), sample_section_size(0), nNumArts(0), arts_offset(0),
      sample_section_offset(0) {
}

AkaoSampColl::AkaoSampColl(RawFile *file, AkaoInstrDatLocation file_location, std::string name)
    : VGMSampColl(AkaoFormat::name, file, 0, 0, std::move(name)), starting_art_id(0), sample_set_id(0),
      version_(AkaoPs1Version::VERSION_1_0), sample_section_size(0), nNumArts(0), arts_offset(0),
      sample_section_offset(0), file_location(file_location)
{
  dwOffset = file_location.instrAllOffset;
  if (dwOffset > file_location.instrAllOffset) {
    dwOffset = file_location.instrAllOffset;
  }
  if (dwOffset > file_location.instrDatOffset) {
    dwOffset = file_location.instrDatOffset;
  }

  sample_section_size = GetWord(file_location.instrAllOffset);
  const uint32_t end_offset = std::max(file_location.instrAllOffset + 0x10 + sample_section_size,
    file_location.instrDatOffset + 64 * file_location.numArticulations);
  unLength = end_offset - dwOffset;
}

bool AkaoSampColl::IsPossibleAkaoSampColl(const RawFile *file, uint32_t offset) {
  if (offset + 0x50 > file->size())
    return false;

  if (file->GetWordBE(offset) != 0x414B414F)
    return false;

  if ((file->GetWord(offset + 0x24) != 0 || file->GetWord(offset + 0x28) != 0 || file->GetWord(offset + 0x2C) != 0) &&
    (file->GetWord(offset + 0x30) != 0 || file->GetWord(offset + 0x34) != 0 || file->GetWord(offset + 0x38)) != 0 &&
    file->GetWord(offset + 0x3C) != 0)
    return false;

  const uint32_t first_dest = file->GetWord(offset + 0x40);
  if (first_dest != 0 && first_dest != file->GetWord(offset + 0x10))
    return false;

  return true;
}

AkaoPs1Version AkaoSampColl::GuessVersion(const RawFile *file, uint32_t offset) {
  if (file->GetWord(offset + 0x40) == 0) {
    const uint32_t num_articulations = file->GetWord(offset + 0x1C);

    if (offset + 0x10 * num_articulations >= file->size())
      return AkaoPs1Version::UNKNOWN;

    for (uint32_t i = 0; i < num_articulations; i++) {
      const uint32_t art_offset = offset + 0x10 * i;

      // verify the higher byte of unity key is 0
      if (file->GetByte(art_offset + 0x0B) != 0)
        return AkaoPs1Version::VERSION_3_0;
    }

    return AkaoPs1Version::VERSION_3_2;
  }
  else if (file->GetWord(offset + 0x18) != 0 || file->GetWord(offset + 0x1C) != 0)
    return AkaoPs1Version::VERSION_2;
  else
    return AkaoPs1Version::VERSION_1_1;
}

bool AkaoSampColl::GetHeaderInfo() {
  if (version() == AkaoPs1Version::UNKNOWN)
    return false;

  //Read Sample Set header info
  if (version() >= AkaoPs1Version::VERSION_3_0) {
    VGMHeader *hdr = AddHeader(dwOffset, 0x40);
    hdr->AddSig(dwOffset, 4);
    hdr->AddSimpleItem(dwOffset + 4, 2, "ID");
    hdr->AddSimpleItem(dwOffset + 0x10, 4, "SPU Destination Address");
    hdr->AddSimpleItem(dwOffset + 0x14, 4, "Sample Section Size");
    hdr->AddSimpleItem(dwOffset + 0x18, 4, "Starting Articulation ID");
    hdr->AddSimpleItem(dwOffset + 0x1C, 4, "Number of Articulations");

    id = GetShort(0x4 + dwOffset);
    sample_section_size = GetWord(0x14 + dwOffset);
    starting_art_id = GetWord(0x18 + dwOffset);
    nNumArts = GetWord(0x1C + dwOffset);
    arts_offset = 0x40 + dwOffset;
  }
  else if (version() >= AkaoPs1Version::VERSION_1_1) {
    VGMHeader *hdr = AddHeader(dwOffset, 0x40);
    hdr->AddSig(dwOffset, 4);
    hdr->AddSimpleItem(dwOffset + 0x10, 4, "SPU Destination Address");
    hdr->AddSimpleItem(dwOffset + 0x14, 4, "Sample Section Size");
    hdr->AddSimpleItem(dwOffset + 0x18, 4, "Starting Articulation ID");
    if (version() >= AkaoPs1Version::VERSION_1_2)
      hdr->AddSimpleItem(dwOffset + 0x1C, 4, "Ending Articulation ID");

    sample_section_size = GetWord(0x14 + dwOffset);
    starting_art_id = GetWord(0x18 + dwOffset);
    uint32_t ending_art_id;
    if (version() == AkaoPs1Version::VERSION_1_1)
      ending_art_id = 0x80;
    else {
      ending_art_id = GetWord(0x1C + dwOffset);
      if (ending_art_id == 0)
        ending_art_id = 0x100;
    }
    nNumArts = ending_art_id - starting_art_id;
    arts_offset = 0x40 + dwOffset;
  }
  else if (version() == AkaoPs1Version::VERSION_1_0) {
    VGMHeader *hdr = AddHeader(file_location.instrAllOffset, 0x10);
    hdr->AddSig(dwOffset, 4);
    hdr->AddSimpleItem(file_location.instrAllOffset, 4, "SPU Destination Address");
    hdr->AddSimpleItem(file_location.instrAllOffset + 4, 4, "Sample Section Size");

    sample_section_offset = file_location.instrAllOffset + 0x10;
    sample_section_size = GetWord(file_location.instrAllOffset + 4);
    starting_art_id = file_location.startingArticulationId;
    nNumArts = file_location.numArticulations;
    arts_offset = file_location.instrDatOffset;
  }
  else
    return false;

  if (nNumArts > 300 || nNumArts == 0)
    return false;

  return true;
}

bool AkaoSampColl::GetSampleInfo() {
  //Read Articulation Data
  const uint32_t kAkaoArtSize = (version() >= AkaoPs1Version::VERSION_3_1) ? 0x10 : 0x40;
  if (arts_offset + kAkaoArtSize * nNumArts > rawfile->size())
    return false;

  for (uint32_t i = 0; i < nNumArts; i++) {
    AkaoArt art;

    if (version() >= AkaoPs1Version::VERSION_3_1) {
      const uint32_t art_offset = arts_offset + i * 0x10;
      VGMHeader *ArtHdr = AddHeader(art_offset, 16, "Articulation");
      ArtHdr->AddSimpleItem(art_offset, 4, "Sample Offset");
      ArtHdr->AddSimpleItem(art_offset + 4, 4, "Loop Point");
      ArtHdr->AddSimpleItem(art_offset + 8, 2, "Fine Tune");
      ArtHdr->AddSimpleItem(art_offset + 10, 2, "Unity Key");
      ArtHdr->AddSimpleItem(art_offset + 12, 2, "ADSR1");
      ArtHdr->AddSimpleItem(art_offset + 14, 2, "ADSR2");

      const int16_t raw_fine_tune = GetShort(art_offset + 8);
      const double freq_multiplier = (raw_fine_tune >= 0)
        ? 1.0 + (raw_fine_tune / 32768.0)
        : static_cast<uint16_t>(raw_fine_tune) / 65536.0;
      const double cents = log(freq_multiplier) / log(2.0) * 1200;
      const auto coarse_tune = static_cast<int8_t>(cents / 100);
      const auto fine_tune = static_cast<int16_t>(static_cast<int>(cents) % 100);

      art.sample_offset = GetWord(art_offset);
      art.loop_point = GetWord(art_offset + 4) - art.sample_offset;
      art.fineTune = fine_tune;
      art.unityKey = static_cast<uint8_t>(GetShort(art_offset + 0xA)) - coarse_tune;
      art.ADSR1 = GetShort(art_offset + 0xC);
      art.ADSR2 = GetShort(art_offset + 0xE);
      art.artID = starting_art_id + i;
    }
    else if (version() == AkaoPs1Version::VERSION_3_0) {
      const uint32_t art_offset = arts_offset + i * 0x40;
      VGMHeader *ArtHdr = AddHeader(art_offset, 0x40, "Articulation");
      ArtHdr->AddSimpleItem(art_offset, 4, "Sample Offset");
      ArtHdr->AddSimpleItem(art_offset + 4, 4, "Loop Point");
      ArtHdr->AddSimpleItem(art_offset + 8, 4, "Base Pitch (C)");
      ArtHdr->AddSimpleItem(art_offset + 0x0C, 4, "Base Pitch (C#)");
      ArtHdr->AddSimpleItem(art_offset + 0x10, 4, "Base Pitch (D)");
      ArtHdr->AddSimpleItem(art_offset + 0x14, 4, "Base Pitch (D#)");
      ArtHdr->AddSimpleItem(art_offset + 0x18, 4, "Base Pitch (E)");
      ArtHdr->AddSimpleItem(art_offset + 0x1C, 4, "Base Pitch (F)");
      ArtHdr->AddSimpleItem(art_offset + 0x20, 4, "Base Pitch (F#)");
      ArtHdr->AddSimpleItem(art_offset + 0x24, 4, "Base Pitch (G)");
      ArtHdr->AddSimpleItem(art_offset + 0x28, 4, "Base Pitch (G#)");
      ArtHdr->AddSimpleItem(art_offset + 0x2C, 4, "Base Pitch (A)");
      ArtHdr->AddSimpleItem(art_offset + 0x30, 4, "Base Pitch (A#)");
      ArtHdr->AddSimpleItem(art_offset + 0x34, 4, "Base Pitch (B)");
      ArtHdr->AddSimpleItem(art_offset + 0x38, 1, "ADSR Attack Rate");
      ArtHdr->AddSimpleItem(art_offset + 0x39, 1, "ADSR Decay Rate");
      ArtHdr->AddSimpleItem(art_offset + 0x3A, 1, "ADSR Sustain Level");
      ArtHdr->AddSimpleItem(art_offset + 0x3B, 1, "ADSR Sustain Rate");
      ArtHdr->AddSimpleItem(art_offset + 0x3C, 1, "ADSR Release Rate");
      ArtHdr->AddSimpleItem(art_offset + 0x3D, 1, "ADSR Attack Mode");
      ArtHdr->AddSimpleItem(art_offset + 0x3E, 1, "ADSR Sustain Mode");
      ArtHdr->AddSimpleItem(art_offset + 0x3F, 1, "ADSR Release Mode");

      const uint8_t ar = GetByte(art_offset + 0x38);
      const uint8_t dr = GetByte(art_offset + 0x39);
      const uint8_t sl = GetByte(art_offset + 0x3A);
      const uint8_t sr = GetByte(art_offset + 0x3B);
      const uint8_t rr = GetByte(art_offset + 0x3C);
      const uint8_t a_mode = GetByte(art_offset + 0x3D);
      const uint8_t s_mode = GetByte(art_offset + 0x3E);
      const uint8_t r_mode = GetByte(art_offset + 0x3F);

      const uint32_t base_pitch = GetWord(art_offset + 8);
      const double freq_multiplier = base_pitch / static_cast<double>(4096 * 256);
      const double cents = log(freq_multiplier) / log(2.0) * 1200;
      const auto coarse_tune = static_cast<int8_t>(cents / 100);
      const auto fine_tune = static_cast<int16_t>(static_cast<int>(cents) % 100);

      art.sample_offset = GetWord(art_offset);
      art.loop_point = GetWord(art_offset + 4) - art.sample_offset;
      art.fineTune = fine_tune;
      art.unityKey = 72 - coarse_tune;
      art.ADSR1 = ComposePSXADSR1((a_mode & 4) >> 2, ar, dr, sl);
      art.ADSR2 = ComposePSXADSR2((s_mode & 4) >> 2, (s_mode & 2) >> 1, sr, (r_mode & 4) >> 2, rr);
      art.artID = starting_art_id + i;
    }
    else if (version() >= AkaoPs1Version::VERSION_1_1) {
      const uint32_t spu_dest_address = GetWord(dwOffset + 0x10);

      const uint32_t art_offset = arts_offset + i * 0x40;
      VGMHeader *ArtHdr = AddHeader(art_offset, 0x40, "Articulation");
      ArtHdr->AddSimpleItem(art_offset, 4, "Sample Offset");
      ArtHdr->AddSimpleItem(art_offset + 4, 4, "Loop Point");
      ArtHdr->AddSimpleItem(art_offset + 8, 1, "ADSR Attack Rate");
      ArtHdr->AddSimpleItem(art_offset + 9, 1, "ADSR Decay Rate");
      ArtHdr->AddSimpleItem(art_offset + 0x0A, 1, "ADSR Sustain Level");
      ArtHdr->AddSimpleItem(art_offset + 0x0B, 1, "ADSR Sustain Rate");
      ArtHdr->AddSimpleItem(art_offset + 0x0C, 1, "ADSR Release Rate");
      ArtHdr->AddSimpleItem(art_offset + 0x0D, 1, "ADSR Attack Mode");
      ArtHdr->AddSimpleItem(art_offset + 0x0E, 1, "ADSR Sustain Mode");
      ArtHdr->AddSimpleItem(art_offset + 0x0F, 1, "ADSR Release Mode");
      ArtHdr->AddSimpleItem(art_offset + 0x10, 4, "Base Pitch (C)");
      ArtHdr->AddSimpleItem(art_offset + 0x14, 4, "Base Pitch (C#)");
      ArtHdr->AddSimpleItem(art_offset + 0x18, 4, "Base Pitch (D)");
      ArtHdr->AddSimpleItem(art_offset + 0x1C, 4, "Base Pitch (D#)");
      ArtHdr->AddSimpleItem(art_offset + 0x20, 4, "Base Pitch (E)");
      ArtHdr->AddSimpleItem(art_offset + 0x24, 4, "Base Pitch (F)");
      ArtHdr->AddSimpleItem(art_offset + 0x28, 4, "Base Pitch (F#)");
      ArtHdr->AddSimpleItem(art_offset + 0x2C, 4, "Base Pitch (G)");
      ArtHdr->AddSimpleItem(art_offset + 0x30, 4, "Base Pitch (G#)");
      ArtHdr->AddSimpleItem(art_offset + 0x34, 4, "Base Pitch (A)");
      ArtHdr->AddSimpleItem(art_offset + 0x38, 4, "Base Pitch (A#)");
      ArtHdr->AddSimpleItem(art_offset + 0x3C, 4, "Base Pitch (B)");

      const uint32_t sample_start_address = GetWord(art_offset);
      const uint32_t loop_start_address = GetWord(art_offset + 4);
      if (sample_start_address < spu_dest_address || loop_start_address < spu_dest_address || sample_start_address > loop_start_address)
        return false;

      const uint8_t ar = GetByte(art_offset + 8);
      const uint8_t dr = GetByte(art_offset + 9);
      const uint8_t sl = GetByte(art_offset + 0x0A);
      const uint8_t sr = GetByte(art_offset + 0x0B);
      const uint8_t rr = GetByte(art_offset + 0x0C);
      const uint8_t a_mode = GetByte(art_offset + 0x0D);
      const uint8_t s_mode = GetByte(art_offset + 0x0E);
      const uint8_t r_mode = GetByte(art_offset + 0x0F);

      const uint32_t base_pitch = GetWord(art_offset + 0x10);
      const double freq_multiplier = base_pitch / 4096.0;
      const double cents = log(freq_multiplier) / log(2.0) * 1200;
      const auto coarse_tune = static_cast<int8_t>(cents / 100);
      const auto fine_tune = static_cast<int16_t>(static_cast<int>(cents) % 100);

      art.sample_offset = sample_start_address - spu_dest_address;
      art.loop_point = loop_start_address - sample_start_address;
      art.fineTune = fine_tune;
      art.unityKey = 72 - coarse_tune;
      art.ADSR1 = ComposePSXADSR1((a_mode & 4) >> 2, ar, dr, sl);
      art.ADSR2 = ComposePSXADSR2((s_mode & 4) >> 2, (s_mode & 2) >> 1, sr, (r_mode & 4) >> 2, rr);
      art.artID = starting_art_id + i;
    }
    else if (version() == AkaoPs1Version::VERSION_1_0) {
      const uint32_t spu_dest_address = GetWord(file_location.instrAllOffset);

      const uint32_t art_offset = file_location.instrDatOffset + i * 0x40;
      VGMHeader *ArtHdr = AddHeader(art_offset, 0x40, "Articulation");
      ArtHdr->AddSimpleItem(art_offset, 4, "Sample Offset");
      ArtHdr->AddSimpleItem(art_offset + 4, 4, "Loop Point");
      ArtHdr->AddSimpleItem(art_offset + 8, 1, "ADSR Attack Rate");
      ArtHdr->AddSimpleItem(art_offset + 9, 1, "ADSR Decay Rate");
      ArtHdr->AddSimpleItem(art_offset + 0x0A, 1, "ADSR Sustain Level");
      ArtHdr->AddSimpleItem(art_offset + 0x0B, 1, "ADSR Sustain Rate");
      ArtHdr->AddSimpleItem(art_offset + 0x0C, 1, "ADSR Release Rate");
      ArtHdr->AddSimpleItem(art_offset + 0x0D, 1, "ADSR Attack Mode");
      ArtHdr->AddSimpleItem(art_offset + 0x0E, 1, "ADSR Sustain Mode");
      ArtHdr->AddSimpleItem(art_offset + 0x0F, 1, "ADSR Release Mode");
      ArtHdr->AddSimpleItem(art_offset + 0x10, 4, "Base Pitch (C)");
      ArtHdr->AddSimpleItem(art_offset + 0x14, 4, "Base Pitch (C#)");
      ArtHdr->AddSimpleItem(art_offset + 0x18, 4, "Base Pitch (D)");
      ArtHdr->AddSimpleItem(art_offset + 0x1C, 4, "Base Pitch (D#)");
      ArtHdr->AddSimpleItem(art_offset + 0x20, 4, "Base Pitch (E)");
      ArtHdr->AddSimpleItem(art_offset + 0x24, 4, "Base Pitch (F)");
      ArtHdr->AddSimpleItem(art_offset + 0x28, 4, "Base Pitch (F#)");
      ArtHdr->AddSimpleItem(art_offset + 0x2C, 4, "Base Pitch (G)");
      ArtHdr->AddSimpleItem(art_offset + 0x30, 4, "Base Pitch (G#)");
      ArtHdr->AddSimpleItem(art_offset + 0x34, 4, "Base Pitch (A)");
      ArtHdr->AddSimpleItem(art_offset + 0x38, 4, "Base Pitch (A#)");
      ArtHdr->AddSimpleItem(art_offset + 0x3C, 4, "Base Pitch (B)");

      const uint32_t sample_start_address = GetWord(art_offset);
      const uint32_t loop_start_address = GetWord(art_offset + 4);
      if (sample_start_address < spu_dest_address || loop_start_address < spu_dest_address || sample_start_address > loop_start_address)
        continue;

      const uint8_t ar = GetByte(art_offset + 8);
      const uint8_t dr = GetByte(art_offset + 9);
      const uint8_t sl = GetByte(art_offset + 0x0A);
      const uint8_t sr = GetByte(art_offset + 0x0B);
      const uint8_t rr = GetByte(art_offset + 0x0C);
      const uint8_t a_mode = GetByte(art_offset + 0x0D);
      const uint8_t s_mode = GetByte(art_offset + 0x0E);
      const uint8_t r_mode = GetByte(art_offset + 0x0F);

      const uint32_t base_pitch = GetWord(art_offset + 0x10);
      const double freq_multiplier = base_pitch / 4096.0;
      const double cents = log(freq_multiplier) / log(2.0) * 1200;
      const auto coarse_tune = static_cast<int8_t>(cents / 100);
      const auto fine_tune = static_cast<int16_t>(static_cast<int>(cents) % 100);

      art.sample_offset = sample_start_address - spu_dest_address;
      art.loop_point = loop_start_address - sample_start_address;
      art.fineTune = fine_tune;
      art.unityKey = 72 - coarse_tune;
      art.ADSR1 = ComposePSXADSR1((a_mode & 4) >> 2, ar, dr, sl);
      art.ADSR2 = ComposePSXADSR2((s_mode & 4) >> 2, (s_mode & 2) >> 1, sr, (r_mode & 4) >> 2, rr);
      art.artID = starting_art_id + i;
    }

    akArts.push_back(art);
  }

  //Time to organize and convert the samples
  //First, we scan through the sample section, and determine the offsets and size of each sample
  //We do this by searchiing for series of 16 0x00 value bytes.  These indicate the beginning of a sample,
  //and they will never be found at any other point within the adpcm sample data.

  //Find sample offsets

  if (version() != AkaoPs1Version::VERSION_1_0)
    sample_section_offset = arts_offset + static_cast<uint32_t>(akArts.size() * kAkaoArtSize);

  // if the official total file size is greater than the file size of the document
  // then shorten the sample section size to the actual end of the document
  if (sample_section_offset + sample_section_size > rawfile->size())
    sample_section_size = static_cast<uint32_t>(rawfile->size()) - sample_section_offset;

  //check the last 10 bytes to make sure they aren't null, if they are, abbreviate things till there is no 0x10 block of null bytes
  if (GetWord(sample_section_offset + sample_section_size - 0x10) == 0) {
    uint32_t j;
    for (j = 0x10; GetWord(sample_section_offset + sample_section_size - j) == 0; j += 0x10);
    sample_section_size -= j - 0x10;        //-0x10 because we went 1 0x10 block too far
  }

  // if the official total file size is greater than the file size of the document
  // then shorten the sample section size to the actual end of the document
  if (sample_section_offset + sample_section_size > rawfile->size())
    sample_section_size = static_cast<uint32_t>(rawfile->size());

  std::set<uint32_t> sample_offsets;
  for (const auto & art : akArts) {
    sample_offsets.insert(art.sample_offset);
  }

  for (const auto & sample_offset : sample_offsets) {
    bool loop;
    const uint32_t offset = sample_section_offset + sample_offset;
    if (offset >= sample_section_offset + sample_section_size) {
      // Out of bounds
      L_ERROR("The sample offset of AkaoRgn exceeds the sample section size. "
              "Offset: 0x{:X} (0x{:X})", sample_offset, offset);
      continue;
    }

    const uint32_t length = PSXSamp::GetSampleLength(rawfile, offset, sample_section_offset + sample_section_size, loop);
    auto *samp = new PSXSamp(this, offset, length, offset, length, 1, 16, 44100,
      fmt::format("Sample {}", samples.size()));

    samples.push_back(samp);
  }

  if (samples.empty())
    return false;

  // now to verify and associate each articulation with a sample index value
  // for every sample of every instrument, we add sample_section offset, because those values
  //  are relative to the beginning of the sample section
  for (auto& akArt : akArts) {
    for (uint32_t l = 0; l < samples.size(); l++) {
      if (akArt.sample_offset + sample_section_offset == samples[l]->dwOffset) {
        akArt.sample_num = l;
        break;
      }
    }
  }

  return true;
}
