#include "pch.h"
#include "AkaoInstr.h"
#include "VGMSamp.h"
#include "PSXSPU.h"

using namespace std;

// ************
// AkaoInstrSet
// ************

AkaoInstrSet::AkaoInstrSet(RawFile *file,
                           uint32_t length,
                           uint32_t instrOff,
                           uint32_t dkitOff,
                           uint32_t theID,
                           wstring name)
    : VGMInstrSet(AkaoFormat::name, file, 0, length, name) {
  id = theID;
  drumkitOff = dkitOff;
  bMelInstrs = instrOff > 0;
  bDrumKit = drumkitOff > 0;
  if (bMelInstrs)
    dwOffset = instrOff;
  else
    dwOffset = drumkitOff;
}

bool AkaoInstrSet::GetInstrPointers() {
  if (bMelInstrs) {
    VGMHeader *SSEQHdr = AddHeader(dwOffset, 0x10, L"Instr Ptr Table");
    int i = 0;
    //-1 aka 0xFFFF if signed or 0 and past the first pointer value
    for (int j = dwOffset; (GetShort(j) != (uint16_t) -1) && ((GetShort(j) != 0) || i == 0) && i < 16; j += 2) {
      SSEQHdr->AddSimpleItem(j, 2, L"Instr Pointer");
      aInstrs.push_back(new AkaoInstr(this, dwOffset + 0x20 + GetShort(j), 0, 0, i++));
    }
  }
  if (bDrumKit)
    aInstrs.push_back(new AkaoDrumKit(this, drumkitOff, 0, 0, 127));
  return true;
}

// *********
// AkaoInstr
// *********

AkaoInstr::AkaoInstr(AkaoInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                     uint32_t theInstrNum, const std::wstring &name)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, name) {
  bDrumKit = false;
}

bool AkaoInstr::LoadInstr() {
  for (int k = 0; (GetWord(dwOffset + k * 8) != 0 || GetWord(dwOffset + k * 8 + 4) != 0) &&
      dwOffset + k * 8 < GetRawFile()->size(); k++) {
    AkaoRgn *rgn = new AkaoRgn(this, dwOffset + k * 8, 8);
    if (!rgn->LoadRgn()) {
      delete rgn;
      return false;
    }
    aRgns.push_back(rgn);
  }
  if (aRgns.size() != 0)
    unLength = aRgns.back()->dwOffset + aRgns.back()->unLength - dwOffset;
  else
    pRoot->AddLogItem(new LogItem(L"Instrument has no regions.", LOG_LEVEL_WARN, L"AkaoInstr"));
  return true;
}

// ***********
// AkaoDrumKit
// ***********

AkaoDrumKit::AkaoDrumKit(AkaoInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                         uint32_t theInstrNum)
    : AkaoInstr(instrSet, offset, length, theBank, theInstrNum, L"Drum Kit") {
  bDrumKit = true;
}

bool AkaoDrumKit::LoadInstr() {
  uint32_t j = dwOffset;  //j = the end of the last instrument of the instrument table ie, the beginning of drumkit data
  uint32_t endOffset = parInstrSet->dwOffset + parInstrSet->unLength;
  for (uint32_t i = 0; (i < 128) && (j < endOffset); i++) {
    //we found some region data for the drum kit
    if ((GetWord(j) != 0) && (GetWord(j + 4) != 0)) {
      //if we run into 0xFFFFFFFF FFFFFFFF, then we quit reading for drumkit regions early
      if ((GetWord(j) == 0xFFFFFFFF) && (GetWord(j + 4) == 0xFFFFFFFF))
        break;

      uint8_t assoc_art_id = GetByte(j + 0); //- first_sample_id;
      uint8_t lowKey = i; //-12;		//-7 CORRECT ?? WHO KNOWS?
      uint8_t highKey = lowKey;  //the region only covers the one key
      AkaoRgn *rgn = (AkaoRgn *) AddRgn(new AkaoRgn(this, j, 8, lowKey, highKey, assoc_art_id));
      rgn->drumRelUnityKey = GetByte(j + 1);
      uint8_t vol = GetByte(j + 6);
      rgn->SetVolume((double) vol / 127.0);
      rgn->AddGeneralItem(j, 1, L"Associated Articulation ID");
      rgn->AddGeneralItem(j + 1, 1, L"Relative Unity Key");
      rgn->AddUnknown(j + 2, 1);
      rgn->AddUnknown(j + 3, 1);
      rgn->AddUnknown(j + 4, 1);
      rgn->AddUnknown(j + 5, 1);
      rgn->AddGeneralItem(j + 6, 1, L"Attenuation");
      rgn->AddPan(GetByte(j + 7), j + 7);
    }
    j += 8;
  }

  if (aRgns.size() != 0)
    unLength = aRgns.back()->dwOffset + aRgns.back()->unLength - dwOffset;
  else
    pRoot->AddLogItem(new LogItem(L"Instrument has no regions.", LOG_LEVEL_WARN, L"AkaoInstr"));
  return true;
}


// *******
// AkaoRgn
// *******
AkaoRgn::AkaoRgn(VGMInstr *instr, uint32_t offset, uint32_t length, uint8_t keyLow, uint8_t keyHigh,
                 uint8_t artIDNum, const std::wstring &name)
    : VGMRgn(instr, offset, length, keyLow, keyHigh, 0, 0x7F, 0), artNum(artIDNum) {
}

AkaoRgn::AkaoRgn(VGMInstr *instr, uint32_t offset, uint32_t length, const std::wstring &name)
    : VGMRgn(instr, offset, length, name) {
}

bool AkaoRgn::LoadRgn() {
  //instrument[i].region[k].fine_tune = stuff[(instrument[i].info_ptr + k*0x20 + 0x12)];
  //AddUnityKey(0x3A - GetByte(dwOffset + k*0x20 + 0x13), dwOffset + k*0x20 + 0x13);
  //instrument[i].region[k].unity_key =		0x3A - stuff[(instrument[i].info_ptr + k*0x20 + 0x13)] ;
  AddGeneralItem(dwOffset + 0, 1, L"Associated Articulation ID");
  artNum = GetByte(dwOffset + 0); //- first_sample_id;
  AddKeyLow(GetByte(dwOffset + 1), dwOffset + 1);
  AddKeyHigh(GetByte(dwOffset + 2), dwOffset + 2);
  //AddVelLow(GetByte(dwOffset + 3), dwOffset + 3);
  //AddVelHigh(GetByte(dwOffset + 4), dwOffset + 4);
  AddUnknown(dwOffset + 3, 1);
  AddUnknown(dwOffset + 4, 1);
  AddUnknown(dwOffset + 5, 1);
  AddUnknown(dwOffset + 6, 1);
  AddVolume(GetByte(dwOffset + 7) / 127.0, dwOffset + 7, 1);

  //if (aInstrs[i]->info_ptr + (k+1)*8 >= aInstrs[i+1]->info_ptr - 8)	//if this is the last region of the instrument
  //	aInstrs[i]->aRegions[k]->last_key = 0x7F;
  //if (k == 0)																//if this is the first region of the instrument
  //	aInstrs[i]->aRegions[k]->first_key = 0;

  //if (keyLow > keyHigh && k > 0)	//if the first key is greater than the last key, and this isn't the first region of the instr
  //{
  //	keyLow = keyHigh+1;
  //	Alert(_T("Funkiness going down: lowKey > highKey (also, k > 0)"));
  //}

/*		if ((k > 0) && (pDoc->GetByte(aInstrs[i]->info_ptr + (k-1)*8 + 1) == aInstrs[i]->aRegions[k]->first_key))
	{
		if ((k > 1) && (pDoc->GetByte(aInstrs[i]->info_ptr + (k-2)*8 + 1) == aInstrs[i]->aRegions[k]->first_key))
		{
			aInstrs[i]->aRegions[k]->first_key += 5;
			if (aInstrs[i]->info_ptr + (k+1)*8 < aInstrs[i+1]->info_ptr - 8)  //if there's another region in the instrument (k+1)
				aInstrs[i]->aRegions[k]->last_key = pDoc->GetByte(aInstrs[i]->info_ptr + (k+1)*8 + 2) - 1;
			else
				aInstrs[i]->aRegions[k]->last_key = 0x7F;
			aInstrs[i]->aRegions[k-1]->first_key = aInstrs[i]->aRegions[k]->first_key-5;
			aInstrs[i]->aRegions[k-1]->last_key =  aInstrs[i]->aRegions[k]->first_key -1;
			aInstrs[i]->aRegions[k-2]->first_key = aInstrs[i]->aRegions[k]->first_key-12;
			aInstrs[i]->aRegions[k-2]->last_key =  aInstrs[i]->aRegions[k]->first_key-6;
			if (k == 2)	//if this is the third region of the instrument, then we need to make sure the first region's first key is 0
				aInstrs[i]->aRegions[k-2]->first_key = 0;
		}
		else
		{
			aInstrs[i]->aRegions[k]->first_key += 5;
			if (aInstrs[i]->info_ptr + (k+1)*8 < aInstrs[i+1]->info_ptr - 8)  //if there's another region in the instrument (k+1)
				aInstrs[i]->aRegions[k]->last_key = pDoc->GetByte(aInstrs[i]->info_ptr + (k+1)*8 + 2) - 1;
			else
				aInstrs[i]->aRegions[k]->last_key = 0x7F;
			aInstrs[i]->aRegions[k-1]->first_key = aInstrs[i]->aRegions[k]->first_key-5;
			aInstrs[i]->aRegions[k-1]->last_key =  aInstrs[i]->aRegions[k]->first_key-1;
			if (k == 1)	//if this is the third region of the instrument, then we need to make sure the first region's first key is 0
				aInstrs[i]->aRegions[k-1]->first_key = 0;
		}
	}*/

  uint8_t attenuation = 0x7F;  //default to no attenuation
  uint8_t pan = 0x40;  //default to center pan

  //aInstrs[i]->aRegions[k]->sample_offset = GetWord(sampleinfo_offset+ aInstrs[i]->aRegions[k]->assoc_art_id *0x10);
  //aInstrs[i]->aRegions[k]->loop_point =	GetWord(sampleinfo_offset+ aInstrs[i]->aRegions[k]->assoc_art_id *0x10 + 4) - GetWord(sampleinfo_offset+ instrument[i].region[k].assoc_art_id *0x10 + 0);//GetWord(sampleinfo_offset+ instrument[i].region[k].assoc_art_id *0x10 + 4) - (instrument[i].region[k].sample_offset + sample_section_offset);
  //aInstrs[i]->aRegions[k]->fine_tune =		GetShort(sampleinfo_offset+ aInstrs[i]->aRegions[k]->assoc_art_id *0x10 + 8);
  //aInstrs[i]->aRegions[k]->unity_key =		GetShort(sampleinfo_offset+ aInstrs[i]->aRegions[k]->assoc_art_id *0x10 + 0xA);
  //aInstrs[i]->aRegions[k]->ADSR1 =			GetShort(sampleinfo_offset+ aInstrs[i]->aRegions[k]->assoc_art_id *0x10 + 0xC);
  //aInstrs[i]->aRegions[k]->ADSR2 =			GetShort(sampleinfo_offset+ aInstrs[i]->aRegions[k]->assoc_art_id *0x10 + 0xE);

  //instrument[i].region[k].vel_range_high =stuff[(instrument[i].info_ptr + k*0x20 + 0x15)];
  //instrument[i].region[k].pan =			stuff[(instrument[i].info_ptr + k*0x20 + 0x17)];
//	AkaoRgn* rgn = AkaoRgn();

  //AddRgn(new AkaoRgn(this, dwOffset+k*8, 8, lowKey, highKey, assoc_art_id));
  return true;
}

// ************
// AkaoSampColl
// ************

AkaoSampColl::AkaoSampColl(RawFile *file, uint32_t offset, uint32_t length, wstring name)
    : VGMSampColl(AkaoFormat::name, file, offset, length, name) {
}

AkaoSampColl::~AkaoSampColl() {
}

bool AkaoSampColl::GetHeaderInfo() {
  //Read Sample Set header info
  VGMHeader *hdr = AddHeader(dwOffset, 0x40);
  hdr->AddSig(dwOffset, 4);
  hdr->AddSimpleItem(dwOffset + 4, 2, L"ID");
  hdr->AddSimpleItem(dwOffset + 0x14, 4, L"Sample Section Size");
  hdr->AddSimpleItem(dwOffset + 0x18, 4, L"Starting Articulation ID");
  hdr->AddSimpleItem(dwOffset + 0x1C, 4, L"Number of Articulations");

  id = GetShort(0x4 + dwOffset);
  sample_section_size = GetWord(0x14 + dwOffset);
  starting_art_id = GetWord(0x18 + dwOffset);
  nNumArts = GetWord(0x1C + dwOffset);
  arts_offset = 0x40 + dwOffset;

  if (nNumArts > 300 || nNumArts == 0)
    return false;

  return true;
}

bool AkaoSampColl::GetSampleInfo() {
  uint32_t i;
  uint32_t j;

  //Read Articulation Data
  for (i = 0; i < nNumArts; i++) {
    VGMHeader *ArtHdr = AddHeader(arts_offset + i * 0x10, 16, L"Articulation");
    ArtHdr->AddSimpleItem(arts_offset + i * 0x10 + 0, 4, L"Sample Offset");
    ArtHdr->AddSimpleItem(arts_offset + i * 0x10 + 4, 4, L"Loop Point");
    ArtHdr->AddSimpleItem(arts_offset + i * 0x10 + 8, 2, L"Fine Tune");
    ArtHdr->AddSimpleItem(arts_offset + i * 0x10 + 10, 2, L"Unity Key");
    ArtHdr->AddSimpleItem(arts_offset + i * 0x10 + 12, 2, L"ADSR1");
    ArtHdr->AddSimpleItem(arts_offset + i * 0x10 + 14, 2, L"ADSR2");

    if (arts_offset + i * 0x10 + 0x10 > rawfile->size())
      return false;

    akArts.push_back(AkaoArt());
    akArts[i].sample_offset = GetWord(arts_offset + i * 0x10);
    akArts[i].loop_point = GetWord(arts_offset + i * 0x10 + 4) - akArts[i].sample_offset;
    akArts[i].fineTune = GetShort(arts_offset + i * 0x10 + 8);
    akArts[i].unityKey = (uint8_t) GetShort(arts_offset + i * 0x10 + 0xA);
    akArts[i].ADSR1 = GetShort(arts_offset + i * 0x10 + 0xC);
    akArts[i].ADSR2 = GetShort(arts_offset + i * 0x10 + 0xE);
    akArts[i].artID = starting_art_id + i;
  }

  //Time to organize and convert the samples
  //First, we scan through the sample section, and determine the offsets and size of each sample
  //We do this by searchiing for series of 16 0x00 value bytes.  These indicate the beginning of a sample,
  //and they will never be found at any other point within the adpcm sample data.

  //Find sample offsets

  //sample_section_offset = SampleSetSize - sample_section_size;

  sample_section_offset = arts_offset + (uint32_t) (akArts.size() * 0x10);

  // if the official total file size is greater than the file size of the document
  // then shorten the sample section size to the actual end of the document
  if (sample_section_offset + sample_section_size > rawfile->size())
    sample_section_size = rawfile->size() - sample_section_offset;

  //check the last 10 bytes to make sure they aren't null, if they are, abbreviate things till there is no 0x10 block of null bytes
  if (GetWord(sample_section_offset + sample_section_size - 0x10) == 0) {
    for (j = 0x10; GetWord(sample_section_offset + sample_section_size - j) == 0; j += 0x10);;
    sample_section_size -= j - 0x10;        //-0x10 because we went 1 0x10 block too far
  }

  // if the official total file size is greater than the file size of the document
  // then shorten the sample section size to the actual end of the document
  if (sample_section_offset + sample_section_size > rawfile->size())
    sample_section_size = rawfile->size(); //- sample_section_offset;
  //AddItem("Samples", ICON_TRACK, FileVGMItem.pTreeItem, sample_section_offset, 0, 0, &AllSampsVGMItem); //add the parent "Samples" tree item

  std::set<uint32_t> sample_offsets;
  for (const auto & art : akArts) {
    sample_offsets.insert(art.sample_offset);
  }

  for (const auto & sample_offset : sample_offsets) {
    wostringstream name;
    name << L"Sample " << samples.size();

    bool loop;
    const uint32_t offset = sample_section_offset + sample_offset;
    if (offset >= sample_section_offset + sample_section_size) {
      // Out of bounds (Example: Another Mind)
      wostringstream message;
      message << L"The sample offset of AkaoRgn exceeds the sample section size."
        << L" Offset: 0x" << std::hex << std::uppercase << sample_offset
        << L" (0x" << std::hex << std::uppercase << offset << L")";
      pRoot->AddLogItem(new LogItem(message.str(), LOG_LEVEL_ERR, L"AkaoSampColl"));
      continue;
    }

    const uint32_t length = PSXSamp::GetSampleLength(rawfile, offset, sample_section_offset + sample_section_size, loop);
    PSXSamp *samp = new PSXSamp(this, offset, length, offset, length, 1, 16, 44100, name.str());

    samples.push_back(samp);
  }

  if (samples.empty())
    return false;

  // now to verify and associate each articulation with a sample index value
  // for every sample of every instrument, we add sample_section offset, because those values
  //  are relative to the beginning of the sample section
  for (i = 0; i < akArts.size(); i++) {
    for (uint32_t l = 0; l < samples.size(); l++) {
      if (akArts[i].sample_offset + sample_section_offset == samples[l]->dwOffset) {
        akArts[i].sample_num = l;
        break;
      }
    }
  }

  return true;
}
