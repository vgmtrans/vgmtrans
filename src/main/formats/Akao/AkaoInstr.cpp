/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AkaoInstr.h"

#include "AkaoSeq.h"
#include "base/Types.h"
#include "PSXSPU.h"
#include "VGMColl.h"
#include "VGMSamp.h"

// ************
// AkaoInstrSet
// ************

AkaoInstrSet::AkaoInstrSet(RawFile *file,
                           u32 length,
                           AkaoPs1Version version,
                           u32 instrOff,
                           u32 dkitOff,
                           u32 id,
                           std::string name)
    : VGMInstrSet(AkaoFormat::name, file, 0, length, std::move(name)), version_(version) {
  setId(id);
  instrSetOff = instrOff;
  drumkitOff = dkitOff;
  bMelInstrs = instrSetOff > 0;
  bDrumKit = drumkitOff > 0;
  if (bMelInstrs)
    setOffset(instrSetOff);
  else
    setOffset(drumkitOff);
  end_boundary_offset = offset() + length;
}

AkaoInstrSet::AkaoInstrSet(RawFile *file, u32 end_boundary_offset,
  AkaoPs1Version version, const std::set<u32>& custom_instrument_addresses,
  const std::set<u32>& drum_instrument_addresses, u32 id, std::string name)
  : VGMInstrSet(AkaoFormat::name, file, 0, 0, std::move(name)), bMelInstrs(false), bDrumKit(false),
  instrSetOff(0), drumkitOff(0), end_boundary_offset(end_boundary_offset), version_(version)
{
  setId(id);
  u32 first_instrument_offset = 0;
  if (!custom_instrument_addresses.empty()) {
    first_instrument_offset = *custom_instrument_addresses.begin();
    setOffset(first_instrument_offset);
  }

  if (!drum_instrument_addresses.empty()) {
    const u32 first_drum_offset = *drum_instrument_addresses.begin();

    if (custom_instrument_addresses.empty())
      setOffset(first_drum_offset);
    else
      setOffset(std::min(first_instrument_offset, first_drum_offset));
  }

  this->custom_instrument_addresses = custom_instrument_addresses;
  this->drum_instrument_addresses = drum_instrument_addresses;
}

AkaoInstrSet::AkaoInstrSet(RawFile *file, u32 offset,
  u32 end_boundary_offset, AkaoPs1Version version, u32 id, std::string name)
    : VGMInstrSet(AkaoFormat::name, file, offset, 0, std::move(name)), bMelInstrs(false),
      bDrumKit(false), instrSetOff(0), drumkitOff(0), end_boundary_offset(end_boundary_offset),
      version_(version)
{
  setId(id);
}

bool AkaoInstrSet::parseInstrPointers() {
  if (bMelInstrs) {
    VGMHeader *SSEQHdr = addHeader(instrSetOff, 0x10, "Instr Ptr Table");
    for (int i = 0; i < 16; ++i) {
      u32 ptrOff = instrSetOff + (i * 2);
      u16 instrPtr = readShort(ptrOff);
      if (instrPtr == 0xFFFF || (instrPtr == 0 && i != 0))
        continue;
      SSEQHdr->addChild(ptrOff, 2, "Instr Pointer");
      aInstrs.push_back(new AkaoInstr(this, instrSetOff + 0x20 + instrPtr, 0, 1, i));
    }
  }
  else if (!custom_instrument_addresses.empty()) {
    u32 instrNum = 0;
    for (const u32 instrOff : custom_instrument_addresses) {
      aInstrs.push_back(new AkaoInstr(this, instrOff, 0, 1, instrNum++));
    }
  }

  if (bDrumKit) {
    aInstrs.push_back(new AkaoDrumKit(this, drumkitOff, 0, 127, 127));
  }
  else if (!drum_instrument_addresses.empty()) {
    u32 instrNum = 127;
    for (const u32 instrOff : drum_instrument_addresses) {
      aInstrs.push_back(new AkaoDrumKit(this, instrOff, 0, 127, instrNum--));
    }
  }

  return true;
}

void AkaoInstrSet::useColl(const VGMColl* coll) {
  const auto* seq = dynamic_cast<const AkaoSeq*>(coll->seq());
  if (seq == nullptr || !seq->usesIndividualArts()) {
    return;
  }

  const auto* akaoColl = dynamic_cast<const AkaoColl*>(coll);
  if (akaoColl == nullptr) {
    return;
  }

  auto mappings = akaoColl->mapSampleCollections();
  auto& artIdToSampleNumMap = std::get<1>(mappings);

  for (auto* vgmsampcoll : coll->sampColls()) {
    const auto* sampcoll = dynamic_cast<const AkaoSampColl*>(vgmsampcoll);
    if (sampcoll == nullptr) {
      continue;
    }

    for (size_t i = 0; i < sampcoll->akArts.size(); i++) {
      const AkaoArt* art = &sampcoll->akArts[i];
      auto* newInstr = new AkaoInstr(this, 0, 0, 0, sampcoll->starting_art_id + static_cast<u32>(i));
      auto* rgn = new AkaoRgn(newInstr, 0, 0);

      if (art->loop_point != 0) {
        rgn->setLoopInfo(1, art->loop_point,
                         sampcoll->samples[art->sample_num]->dataLength - art->loop_point);
      }

      if (auto itSampleNum = artIdToSampleNumMap.find(art->artID); itSampleNum != artIdToSampleNumMap.end()) {
        rgn->setSampNum(itSampleNum->second);
      }

      psxConvADSR<AkaoRgn>(rgn, art->ADSR1, art->ADSR2, false);
      rgn->unityKey = art->unityKey;
      rgn->fineTune = art->fineTune;

      newInstr->addRgn(rgn);
      addTempInstr(newInstr);
    }
  }
}

// *********
// AkaoInstr
// *********

AkaoInstr::AkaoInstr(AkaoInstrSet *instrSet, u32 offset, u32 length, u32 bank,
                     u32 instrNum, std::string name)
    : VGMInstr(instrSet, offset, length, bank, instrNum, std::move(name)), bDrumKit(false) {
}

bool AkaoInstr::loadInstr() {
  for (int k = 0; offset() + k * 8 < rawFile()->size(); k++) {
    if (version() < AkaoPs1Version::VERSION_3_0) {
      if (readByte(offset() + k * 8) >= 0x80) {
        addChild(offset() + k * 8, 8, "Region Terminator");
        break;
      }
    }
    else {
      if (getWord(offset() + k * 8) == 0) {
        addChild(offset() + k * 8, 8, "Region Terminator");
        break;
      }
    }

    auto rgn = new AkaoRgn(this, offset() + k * 8, 8);
    if (!rgn->loadRgn()) {
      delete rgn;
      return false;
    }
    if (k > 0) {
      // Some tracks have apparently malformed key low / key high regions, where sections of the
      // keyboard are left unaccounted for, but notes still play in these regions.
      // For ex: Saga Frontier 2 - Thema instrument 0 (harp). The logic below ensures
      // all keys are covered, though it is imperfect as the wrong regions are being extended.
      auto prevRgn = regions().back();
      if (rgn->keyHigh > prevRgn->keyHigh && rgn->keyLow > prevRgn->keyHigh) {
        addRgn(rgn);
      } else if (rgn->keyHigh == prevRgn->keyHigh) {
        // TODO: replace the last region with this one?
      }
      if (rgn->keyLow - prevRgn->keyHigh > 1) {
        rgn->keyLow = prevRgn->keyHigh + 1;
      }
    } else {
      addRgn(rgn);
    }
  }
  if (!regions().empty()) {
    regions().front()->keyLow = 0;
    regions().back()->keyHigh = 0x7F;
  }
  setGuessedLength();

  if (regions().empty())
    L_WARN("Instrument has no regions.");

  return true;
}

// ***********
// AkaoDrumKit
// ***********

AkaoDrumKit::AkaoDrumKit(AkaoInstrSet *instrSet, u32 offset, u32 length, u32 bank,
                         u32 instrNum)
    : AkaoInstr(instrSet, offset, length, bank, instrNum, "Drum Kit") {
  bDrumKit = true;
}

bool AkaoDrumKit::loadInstr() {
  if (version() >= AkaoPs1Version::VERSION_3_0) {
    for (u8 drum_note_number = 0; drum_note_number < 128; drum_note_number++) {
      constexpr u32 kRgnLength = 8;
      const u32 rgn_offset = offset() + drum_note_number * kRgnLength;
      if (rgn_offset + kRgnLength > instrSet()->end_boundary_offset)
        break;

      // skip the region if zero-filled
      if (getWord(rgn_offset) == 0 && getWord(rgn_offset + 4) == 0)
        continue;

      // if we run into 0xFFFFFFFF FFFFFFFF, then we quit reading for drumkit regions early
      //
      // Is this necessary? Is there any real-world case? (loveemu)
      if (getWord(rgn_offset) == 0xFFFFFFFF && getWord(rgn_offset + 4) == 0xFFFFFFFF)
        break;

      const u8 assoc_art_id = readByte(rgn_offset + 0);
      auto *rgn = new AkaoRgn(this, rgn_offset, kRgnLength, drum_note_number, drum_note_number, assoc_art_id);
      addRgn(rgn);
      rgn->drumRelUnityKey = readByte(rgn_offset + 1);
      const u8 raw_volume = readByte(rgn_offset + 6);
      const double volume = raw_volume == 0 ? 1.0 : raw_volume / 128.0;
      rgn->setVolume(volume);
      rgn->addGeneralItem(rgn_offset, 1, "Associated Articulation ID");
      rgn->addGeneralItem(rgn_offset + 1, 1, "Relative Unity Key");
      rgn->attackRate = readByte(offset() + 2);
      rgn->sustainRate = readByte(offset() + 3);
      rgn->sustainMode = readByte(offset() + 4);
      rgn->releaseRate = readByte(offset() + 5);
      rgn->addADSRValue(rgn_offset + 2, 1, "ADSR Attack Rate");
      rgn->addADSRValue(rgn_offset + 3, 1, "ADSR Sustain Rate");
      rgn->addADSRValue(rgn_offset + 4, 1, "ADSR Sustain Mode");
      rgn->addADSRValue(rgn_offset + 5, 1, "ADSR Release Rate");
      rgn->addGeneralItem(rgn_offset + 6, 1, "Attenuation");
      const u8 raw_pan_reverb = readByte(rgn_offset + 7);
      const u8 pan = raw_pan_reverb & 0x7f;
      // Bit 7 is the region reverb enable flag.
      rgn->addGeneralItem(rgn_offset + 7, 1, "Pan & Reverb On/Off");
      rgn->setPan(pan);
      // TODO: set reverb on/off to the region
    }
  }
  else if (version() >= AkaoPs1Version::VERSION_1_0) {
    const u32 kRgnLength = version() >= AkaoPs1Version::VERSION_2 ? 6 : 5;
    for (u8 drum_key = 0; drum_key < 12; drum_key++) {
      constexpr u8 drum_octave = 2; // a drum note ignores octave, this is the octave number for midi remapping
      const u32 rgn_offset = offset() + drum_key * kRgnLength;
      if (rgn_offset + kRgnLength > instrSet()->end_boundary_offset)
        break;

      // skip the region if zero-filled
      if (getWord(rgn_offset) == 0 && readByte(rgn_offset + 4) == 0
        && (version() < AkaoPs1Version::VERSION_2 || readByte(rgn_offset + 5) == 0))
        continue;

      const u8 assoc_art_id = readByte(rgn_offset + 0);
      const u8 drum_note_number = drum_octave * 12 + drum_key;
      auto *rgn = new AkaoRgn(this, rgn_offset, kRgnLength, drum_note_number, drum_note_number, assoc_art_id);
      addRgn(rgn);
      rgn->drumRelUnityKey = readByte(rgn_offset + 1);
      const u16 raw_volume = getWord(rgn_offset + 2);
      rgn->setVolume(raw_volume / (127 * 128.0));
      rgn->addGeneralItem(rgn_offset, 1, "Associated Articulation ID");
      rgn->addGeneralItem(rgn_offset + 1, 1, "Relative Unity Key");
      rgn->addGeneralItem(rgn_offset + 2, 2, "Attenuation");
      const u8 pan = readByte(rgn_offset + 4);
      rgn->addPan(pan, rgn_offset + 4, 1);

      // TODO: set reverb on/off to the region
      if (version() >= AkaoPs1Version::VERSION_2) {
        rgn->addGeneralItem(rgn_offset + 5, 1, "Reverb On/Off");
      }
    }
  }
  else
    return false;

  setGuessedLength();

  if (regions().empty())
    L_WARN("Instrument has no regions.");

  return true;
}


// *******
// AkaoRgn
// *******
AkaoRgn::AkaoRgn(VGMInstr *instr, u32 offset, u32 length, u8 keyLow, u8 keyHigh,
                 u8 artIDNum, std::string name)
    : VGMRgn(instr, offset, length, keyLow, keyHigh, 0, 0x7F, 0, std::move(name)),
      adsr1(0), adsr2(0), artNum(artIDNum), drumRelUnityKey(0) {
}

AkaoRgn::AkaoRgn(VGMInstr *instr, u32 offset, u32 length, std::string name)
    : VGMRgn(instr, offset, length, std::move(name)), adsr1(0), adsr2(0), artNum(0), drumRelUnityKey(0) {
}

bool AkaoRgn::loadRgn() {
  addGeneralItem(offset() + 0, 1, "Associated Articulation ID");
  artNum = readByte(offset() + 0); //- first_sample_id;
  addKeyLow(readByte(offset() + 1), offset() + 1);
  addKeyHigh(readByte(offset() + 2), offset() + 2);
  attackRate = readByte(offset() + 3);
  sustainRate = readByte(offset() + 4);
  sustainMode = readByte(offset() + 5);
  releaseRate = readByte(offset() + 6);
  // TODO: Need to confirm the details of these ADSR values. Sustain Mode values are using 3 bits
  //  which indicates it's more than sustain mode.
  addADSRValue(offset() + 3, 1, "ADSR Attack Rate");
  addADSRValue(offset() + 4, 1, "ADSR Sustain Rate");
  addADSRValue(offset() + 5, 1, "ADSR Sustain Mode");
  addADSRValue(offset() + 6, 1, "ADSR Release Rate");
  const u8 raw_volume = readByte(offset() + 7);
  const double volume = raw_volume == 0 ? 1.0 : raw_volume / 128.0;
  addVolume(volume, offset() + 7, 1);

  return true;
}

// ************
// AkaoSampColl
// ************

AkaoSampColl::AkaoSampColl(RawFile *file, u32 offset, AkaoPs1Version version, std::string name)
    : VGMSampColl(AkaoFormat::name, file, offset, 0, std::move(name)), starting_art_id(0), ending_art_id(0),
      nNumArts(0), sample_set_id(0), version_(version), sample_section_size(0), arts_offset(0),
      sample_section_offset(0) {
}

AkaoSampColl::AkaoSampColl(RawFile *file, AkaoInstrDatLocation file_location, std::string name)
    : VGMSampColl(AkaoFormat::name, file, 0, 0, std::move(name)), starting_art_id(0), ending_art_id(0),
      nNumArts(0), sample_set_id(0), version_(AkaoPs1Version::VERSION_1_0), sample_section_size(0), arts_offset(0),
      sample_section_offset(0), file_location(file_location)
{
  setOffset(file_location.instrAllOffset);
  if (offset() > file_location.instrAllOffset) {
    setOffset(file_location.instrAllOffset);
  }
  if (offset() > file_location.instrDatOffset) {
    setOffset(file_location.instrDatOffset);
  }

  sample_section_size = readWord(file_location.instrAllOffset);
  const u32 end_offset = std::max(file_location.instrAllOffset + 0x10 + sample_section_size,
    file_location.instrDatOffset + 64 * file_location.numArticulations);
  setLength(end_offset - offset());
}

bool AkaoSampColl::isPossibleAkaoSampColl(const RawFile *file, u32 offset) {
  if (offset + 0x50 > file->size())
    return false;

  if (file->readWordBE(offset) != 0x414B414F)
    return false;

  if ((file->readWord(offset + 0x24) != 0 || file->readWord(offset + 0x28) != 0 || file->readWord(offset + 0x2C) != 0) &&
    (file->readWord(offset + 0x30) != 0 || file->readWord(offset + 0x34) != 0 || file->readWord(offset + 0x38)) != 0 &&
    file->readWord(offset + 0x3C) != 0)
    return false;

  const u32 first_dest = file->readWord(offset + 0x40);
  if (first_dest != 0 && first_dest != file->readWord(offset + 0x10))
    return false;

  return true;
}

AkaoPs1Version AkaoSampColl::guessVersion(const RawFile *file, u32 offset) {
  if (file->readWord(offset + 0x40) == 0) {
    const u32 num_articulations = file->readWord(offset + 0x1C);

    if (offset + 0x10 * num_articulations >= file->size())
      return AkaoPs1Version::UNKNOWN;

    for (u32 i = 0; i < num_articulations; i++) {
      const u32 art_offset = offset + 0x10 * i;

      // verify the higher byte of unity key is 0
      if (file->readByte(art_offset + 0x0B) != 0)
        return AkaoPs1Version::VERSION_3_0;
    }

    return AkaoPs1Version::VERSION_3_2;
  }
  else if (file->readWord(offset + 0x18) != 0 || file->readWord(offset + 0x1C) != 0)
    return AkaoPs1Version::VERSION_2;
  else
    return AkaoPs1Version::VERSION_1_1;
}

bool AkaoSampColl::parseHeader() {
  if (version() == AkaoPs1Version::UNKNOWN)
    return false;

  //Read Sample Set header info
  if (version() >= AkaoPs1Version::VERSION_3_0) {
    VGMHeader *hdr = addHeader(offset(), 0x40);
    hdr->addSig(offset(), 4);
    hdr->addChild(offset() + 4, 2, "ID");
    hdr->addChild(offset() + 0x10, 4, "SPU Destination Address");
    hdr->addChild(offset() + 0x14, 4, "Sample Section Size");
    hdr->addChild(offset() + 0x18, 4, "Starting Articulation ID");
    hdr->addChild(offset() + 0x1C, 4, "Number of Articulations");

    setId(readShort(0x4 + offset()));
    sample_section_size = readWord(0x14 + offset());
    starting_art_id = readWord(0x18 + offset());
    nNumArts = readWord(0x1C + offset());
    arts_offset = 0x40 + offset();
  }
  else if (version() >= AkaoPs1Version::VERSION_1_1) {
    VGMHeader *hdr = addHeader(offset(), 0x40);
    hdr->addSig(offset(), 4);
    hdr->addChild(offset() + 0x10, 4, "SPU Destination Address");
    hdr->addChild(offset() + 0x14, 4, "Sample Section Size");
    hdr->addChild(offset() + 0x18, 4, "Starting Articulation ID");
    if (version() >= AkaoPs1Version::VERSION_1_2)
      hdr->addChild(offset() + 0x1C, 4, "Ending Articulation ID");

    sample_section_size = readWord(0x14 + offset());
    starting_art_id = readWord(0x18 + offset());
    if (version() == AkaoPs1Version::VERSION_1_1)
      ending_art_id = 0x80;
    else {
      ending_art_id = readWord(0x1C + offset());
      if (ending_art_id == 0)
        ending_art_id = 0x100;
    }
    nNumArts = ending_art_id - starting_art_id;
    arts_offset = 0x40 + offset();
  }
  else if (version() == AkaoPs1Version::VERSION_1_0) {
    VGMHeader *hdr = addHeader(file_location.instrAllOffset, 0x10);
    hdr->addSig(offset(), 4);
    hdr->addChild(file_location.instrAllOffset, 4, "SPU Destination Address");
    hdr->addChild(file_location.instrAllOffset + 4, 4, "Sample Section Size");

    sample_section_offset = file_location.instrAllOffset + 0x10;
    sample_section_size = readWord(file_location.instrAllOffset + 4);
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

bool AkaoSampColl::parseSampleInfo() {
  //Read Articulation Data
  const u32 kAkaoArtSize = (version() >= AkaoPs1Version::VERSION_3_1) ? 0x10 : 0x40;
  if (arts_offset + kAkaoArtSize * nNumArts > rawFile()->size())
    return false;

  for (u32 i = 0; i < nNumArts; i++) {
    AkaoArt art;

    if (version() >= AkaoPs1Version::VERSION_3_1) {
      const u32 art_offset = arts_offset + i * 0x10;
      VGMHeader *ArtHdr = addHeader(art_offset, 16, "Articulation");
      ArtHdr->addChild(art_offset, 4, "Sample Offset");
      ArtHdr->addChild(art_offset + 4, 4, "Loop Point");
      ArtHdr->addChild(art_offset + 8, 2, "Fine Tune");
      ArtHdr->addChild(art_offset + 10, 2, "Unity Key");
      ArtHdr->addChild(art_offset + 12, 2, "ADSR1");
      ArtHdr->addChild(art_offset + 14, 2, "ADSR2");

      const s16 raw_fine_tune = readShort(art_offset + 8);
      const double freq_multiplier = (raw_fine_tune >= 0)
        ? 1.0 + (raw_fine_tune / 32768.0)
        : static_cast<u16>(raw_fine_tune) / 65536.0;
      const double cents = log(freq_multiplier) / log(2.0) * 1200;
      const auto coarse_tune = static_cast<s8>(cents / 100);
      const auto fine_tune = static_cast<s16>(static_cast<int>(cents) % 100);

      art.sample_offset = readWord(art_offset);
      art.loop_point = readWord(art_offset + 4) - art.sample_offset;
      art.fineTune = fine_tune;
      art.unityKey = static_cast<u8>(readShort(art_offset + 0xA)) - coarse_tune;
      art.ADSR1 = readShort(art_offset + 0xC);
      art.ADSR2 = readShort(art_offset + 0xE);
      art.artID = starting_art_id + i;
    }
    else if (version() == AkaoPs1Version::VERSION_3_0) {
      const u32 art_offset = arts_offset + i * 0x40;
      VGMHeader *ArtHdr = addHeader(art_offset, 0x40, "Articulation");
      ArtHdr->addChild(art_offset, 4, "Sample Offset");
      ArtHdr->addChild(art_offset + 4, 4, "Loop Point");
      ArtHdr->addChild(art_offset + 8, 4, "Base Pitch (C)");
      ArtHdr->addChild(art_offset + 0x0C, 4, "Base Pitch (C#)");
      ArtHdr->addChild(art_offset + 0x10, 4, "Base Pitch (D)");
      ArtHdr->addChild(art_offset + 0x14, 4, "Base Pitch (D#)");
      ArtHdr->addChild(art_offset + 0x18, 4, "Base Pitch (E)");
      ArtHdr->addChild(art_offset + 0x1C, 4, "Base Pitch (F)");
      ArtHdr->addChild(art_offset + 0x20, 4, "Base Pitch (F#)");
      ArtHdr->addChild(art_offset + 0x24, 4, "Base Pitch (G)");
      ArtHdr->addChild(art_offset + 0x28, 4, "Base Pitch (G#)");
      ArtHdr->addChild(art_offset + 0x2C, 4, "Base Pitch (A)");
      ArtHdr->addChild(art_offset + 0x30, 4, "Base Pitch (A#)");
      ArtHdr->addChild(art_offset + 0x34, 4, "Base Pitch (B)");
      ArtHdr->addChild(art_offset + 0x38, 1, "ADSR Attack Rate");
      ArtHdr->addChild(art_offset + 0x39, 1, "ADSR Decay Rate");
      ArtHdr->addChild(art_offset + 0x3A, 1, "ADSR Sustain Level");
      ArtHdr->addChild(art_offset + 0x3B, 1, "ADSR Sustain Rate");
      ArtHdr->addChild(art_offset + 0x3C, 1, "ADSR Release Rate");
      ArtHdr->addChild(art_offset + 0x3D, 1, "ADSR Attack Mode");
      ArtHdr->addChild(art_offset + 0x3E, 1, "ADSR Sustain Mode");
      ArtHdr->addChild(art_offset + 0x3F, 1, "ADSR Release Mode");

      const u8 ar = readByte(art_offset + 0x38);
      const u8 dr = readByte(art_offset + 0x39);
      const u8 sl = readByte(art_offset + 0x3A);
      const u8 sr = readByte(art_offset + 0x3B);
      const u8 rr = readByte(art_offset + 0x3C);
      const u8 a_mode = readByte(art_offset + 0x3D);
      const u8 s_mode = readByte(art_offset + 0x3E);
      const u8 r_mode = readByte(art_offset + 0x3F);

      const u32 base_pitch = readWord(art_offset + 8);
      const double freq_multiplier = base_pitch / static_cast<double>(4096 * 256);
      const double cents = log(freq_multiplier) / log(2.0) * 1200;
      const auto coarse_tune = static_cast<s8>(cents / 100);
      const auto fine_tune = static_cast<s16>(static_cast<int>(cents) % 100);

      art.sample_offset = readWord(art_offset);
      art.loop_point = readWord(art_offset + 4) - art.sample_offset;
      art.fineTune = fine_tune;
      art.unityKey = 72 - coarse_tune;
      art.ADSR1 = composePSXADSR1((a_mode & 4) >> 2, ar, dr, sl);
      art.ADSR2 = composePSXADSR2((s_mode & 4) >> 2, (s_mode & 2) >> 1, sr, (r_mode & 4) >> 2, rr);
      art.artID = starting_art_id + i;
    }
    else if (version() >= AkaoPs1Version::VERSION_1_1) {
      const u32 spu_dest_address = readWord(offset() + 0x10);

      const u32 art_offset = arts_offset + i * 0x40;
      VGMHeader *ArtHdr = addHeader(art_offset, 0x40, "Articulation");
      ArtHdr->addChild(art_offset, 4, "Sample Offset");
      ArtHdr->addChild(art_offset + 4, 4, "Loop Point");
      ArtHdr->addChild(art_offset + 8, 1, "ADSR Attack Rate");
      ArtHdr->addChild(art_offset + 9, 1, "ADSR Decay Rate");
      ArtHdr->addChild(art_offset + 0x0A, 1, "ADSR Sustain Level");
      ArtHdr->addChild(art_offset + 0x0B, 1, "ADSR Sustain Rate");
      ArtHdr->addChild(art_offset + 0x0C, 1, "ADSR Release Rate");
      ArtHdr->addChild(art_offset + 0x0D, 1, "ADSR Attack Mode");
      ArtHdr->addChild(art_offset + 0x0E, 1, "ADSR Sustain Mode");
      ArtHdr->addChild(art_offset + 0x0F, 1, "ADSR Release Mode");
      ArtHdr->addChild(art_offset + 0x10, 4, "Base Pitch (C)");
      ArtHdr->addChild(art_offset + 0x14, 4, "Base Pitch (C#)");
      ArtHdr->addChild(art_offset + 0x18, 4, "Base Pitch (D)");
      ArtHdr->addChild(art_offset + 0x1C, 4, "Base Pitch (D#)");
      ArtHdr->addChild(art_offset + 0x20, 4, "Base Pitch (E)");
      ArtHdr->addChild(art_offset + 0x24, 4, "Base Pitch (F)");
      ArtHdr->addChild(art_offset + 0x28, 4, "Base Pitch (F#)");
      ArtHdr->addChild(art_offset + 0x2C, 4, "Base Pitch (G)");
      ArtHdr->addChild(art_offset + 0x30, 4, "Base Pitch (G#)");
      ArtHdr->addChild(art_offset + 0x34, 4, "Base Pitch (A)");
      ArtHdr->addChild(art_offset + 0x38, 4, "Base Pitch (A#)");
      ArtHdr->addChild(art_offset + 0x3C, 4, "Base Pitch (B)");

      const u32 sample_start_address = readWord(art_offset);
      const u32 loop_start_address = readWord(art_offset + 4);
      if (sample_start_address < spu_dest_address || loop_start_address < spu_dest_address || sample_start_address > loop_start_address)
        return false;

      const u8 ar = readByte(art_offset + 8);
      const u8 dr = readByte(art_offset + 9);
      const u8 sl = readByte(art_offset + 0x0A);
      const u8 sr = readByte(art_offset + 0x0B);
      const u8 rr = readByte(art_offset + 0x0C);
      const u8 a_mode = readByte(art_offset + 0x0D);
      const u8 s_mode = readByte(art_offset + 0x0E);
      const u8 r_mode = readByte(art_offset + 0x0F);

      const u32 base_pitch = readWord(art_offset + 0x10);
      const double freq_multiplier = base_pitch / 4096.0;
      const double cents = log(freq_multiplier) / log(2.0) * 1200;
      const auto coarse_tune = static_cast<s8>(cents / 100);
      const auto fine_tune = static_cast<s16>(static_cast<int>(cents) % 100);

      art.sample_offset = sample_start_address - spu_dest_address;
      art.loop_point = loop_start_address - sample_start_address;
      art.fineTune = fine_tune;
      art.unityKey = 72 - coarse_tune;
      art.ADSR1 = composePSXADSR1((a_mode & 4) >> 2, ar, dr, sl);
      art.ADSR2 = composePSXADSR2((s_mode & 4) >> 2, (s_mode & 2) >> 1, sr, (r_mode & 4) >> 2, rr);
      art.artID = starting_art_id + i;
    }
    else if (version() == AkaoPs1Version::VERSION_1_0) {
      const u32 spu_dest_address = readWord(file_location.instrAllOffset);

      const u32 art_offset = file_location.instrDatOffset + i * 0x40;
      VGMHeader *ArtHdr = addHeader(art_offset, 0x40, "Articulation");
      ArtHdr->addChild(art_offset, 4, "Sample Offset");
      ArtHdr->addChild(art_offset + 4, 4, "Loop Point");
      ArtHdr->addChild(art_offset + 8, 1, "ADSR Attack Rate");
      ArtHdr->addChild(art_offset + 9, 1, "ADSR Decay Rate");
      ArtHdr->addChild(art_offset + 0x0A, 1, "ADSR Sustain Level");
      ArtHdr->addChild(art_offset + 0x0B, 1, "ADSR Sustain Rate");
      ArtHdr->addChild(art_offset + 0x0C, 1, "ADSR Release Rate");
      ArtHdr->addChild(art_offset + 0x0D, 1, "ADSR Attack Mode");
      ArtHdr->addChild(art_offset + 0x0E, 1, "ADSR Sustain Mode");
      ArtHdr->addChild(art_offset + 0x0F, 1, "ADSR Release Mode");
      ArtHdr->addChild(art_offset + 0x10, 4, "Base Pitch (C)");
      ArtHdr->addChild(art_offset + 0x14, 4, "Base Pitch (C#)");
      ArtHdr->addChild(art_offset + 0x18, 4, "Base Pitch (D)");
      ArtHdr->addChild(art_offset + 0x1C, 4, "Base Pitch (D#)");
      ArtHdr->addChild(art_offset + 0x20, 4, "Base Pitch (E)");
      ArtHdr->addChild(art_offset + 0x24, 4, "Base Pitch (F)");
      ArtHdr->addChild(art_offset + 0x28, 4, "Base Pitch (F#)");
      ArtHdr->addChild(art_offset + 0x2C, 4, "Base Pitch (G)");
      ArtHdr->addChild(art_offset + 0x30, 4, "Base Pitch (G#)");
      ArtHdr->addChild(art_offset + 0x34, 4, "Base Pitch (A)");
      ArtHdr->addChild(art_offset + 0x38, 4, "Base Pitch (A#)");
      ArtHdr->addChild(art_offset + 0x3C, 4, "Base Pitch (B)");

      const u32 sample_start_address = readWord(art_offset);
      const u32 loop_start_address = readWord(art_offset + 4);
      if (sample_start_address < spu_dest_address || loop_start_address < spu_dest_address || sample_start_address > loop_start_address)
        continue;

      const u8 ar = readByte(art_offset + 8);
      const u8 dr = readByte(art_offset + 9);
      const u8 sl = readByte(art_offset + 0x0A);
      const u8 sr = readByte(art_offset + 0x0B);
      const u8 rr = readByte(art_offset + 0x0C);
      const u8 a_mode = readByte(art_offset + 0x0D);
      const u8 s_mode = readByte(art_offset + 0x0E);
      const u8 r_mode = readByte(art_offset + 0x0F);

      const u32 base_pitch = readWord(art_offset + 0x10);
      const double freq_multiplier = base_pitch / 4096.0;
      const double cents = log(freq_multiplier) / log(2.0) * 1200;
      const auto coarse_tune = static_cast<s8>(cents / 100);
      const auto fine_tune = static_cast<s16>(static_cast<int>(cents) % 100);

      art.sample_offset = sample_start_address - spu_dest_address;
      art.loop_point = loop_start_address - sample_start_address;
      art.fineTune = fine_tune;
      art.unityKey = 72 - coarse_tune;
      art.ADSR1 = composePSXADSR1((a_mode & 4) >> 2, ar, dr, sl);
      art.ADSR2 = composePSXADSR2((s_mode & 4) >> 2, (s_mode & 2) >> 1, sr, (r_mode & 4) >> 2, rr);
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
    sample_section_offset = arts_offset + static_cast<u32>(akArts.size() * kAkaoArtSize);

  // if the official total file size is greater than the file size of the document
  // then shorten the sample section size to the actual end of the document
  if (sample_section_offset + sample_section_size > rawFile()->size())
    sample_section_size = static_cast<u32>(rawFile()->size()) - sample_section_offset;

  //check the last 10 bytes to make sure they aren't null, if they are, abbreviate things till there is no 0x10 block of null bytes
  if (readWord(sample_section_offset + sample_section_size - 0x10) == 0) {
    u32 j;
    for (j = 0x10; readWord(sample_section_offset + sample_section_size - j) == 0; j += 0x10);
    sample_section_size -= j - 0x10;        //-0x10 because we went 1 0x10 block too far
  }

  // if the official total file size is greater than the file size of the document
  // then shorten the sample section size to the actual end of the document
  if (sample_section_offset + sample_section_size > rawFile()->size())
    sample_section_size = static_cast<u32>(rawFile()->size());

  std::set<u32> sample_offsets;
  for (const auto & art : akArts) {
    sample_offsets.insert(art.sample_offset);
  }

  for (const auto & sample_offset : sample_offsets) {
    bool loop;
    const u32 offset = sample_section_offset + sample_offset;
    if (offset >= sample_section_offset + sample_section_size) {
      // Out of bounds
      L_ERROR("The sample offset of AkaoRgn exceeds the sample section size. "
              "Offset: 0x{:X} (0x{:X})", sample_offset, offset);
      continue;
    }

    const u32 length = PSXSamp::getSampleLength(rawFile(), offset, sample_section_offset + sample_section_size, loop);
    auto *samp = new PSXSamp(this, offset, length, offset, length, 1, BPS::PCM16, 44100,
      fmt::format("Sample {}", samples.size()));

    samples.push_back(samp);
  }

  if (samples.empty())
    return false;

  // now to verify and associate each articulation with a sample index value
  // for every sample of every instrument, we add sample_section offset, because those values
  //  are relative to the beginning of the sample section
  for (auto& akArt : akArts) {
    for (u32 l = 0; l < samples.size(); l++) {
      if (akArt.sample_offset + sample_section_offset == samples[l]->offset()) {
        akArt.sample_num = l;
        break;
      }
    }
  }

  return true;
}
