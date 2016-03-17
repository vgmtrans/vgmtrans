#include "pch.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMInstrSet.h"
#include "Root.h"

using namespace std;

// ***********
// VGMSampColl
// ***********

DECLARE_MENU(VGMSampColl)

VGMSampColl::VGMSampColl(const string &format, RawFile *rawfile, uint32_t offset, uint32_t length, wstring theName)
    : VGMFile(FILETYPE_SAMPCOLL, format, rawfile, offset, length, theName),
      parInstrSet(NULL),
      bLoadOnInstrSetMatch(false),
      bLoaded(false),
      sampDataOffset(0) {
  AddContainer<VGMSamp>(samples);
}

VGMSampColl::VGMSampColl(const string &format, RawFile *rawfile, VGMInstrSet *instrset,
                         uint32_t offset, uint32_t length, wstring theName)
    : VGMFile(FILETYPE_SAMPCOLL, format, rawfile, offset, length, theName),
      parInstrSet(instrset),
      bLoadOnInstrSetMatch(false),
      bLoaded(false),
      sampDataOffset(0) {
  AddContainer<VGMSamp>(samples);
}

VGMSampColl::~VGMSampColl(void) {
  DeleteVect<VGMSamp>(samples);
}


bool VGMSampColl::Load() {
  if (bLoaded)
    return true;
  if (!GetHeaderInfo())
    return false;
  if (!GetSampleInfo())
    return false;

  if (samples.size() == 0)
    return false;

  if (unLength == 0) {
    for (std::vector<VGMSamp *>::iterator itr = samples.begin(); itr != samples.end(); ++itr) {
      VGMSamp *samp = (*itr);

      // Some formats can have negative sample offset
      // For example, Konami's SNES format and Hudson's SNES format
      // TODO: Fix negative sample offset without breaking instrument
      //assert(dwOffset <= samp->dwOffset);

      //if (dwOffset > samp->dwOffset)
      //{
      //	unLength += samp->dwOffset - dwOffset;
      //	dwOffset = samp->dwOffset;
      //}

      if (dwOffset + unLength < samp->dwOffset + samp->unLength) {
        unLength = (samp->dwOffset + samp->unLength) - dwOffset;
      }
    }
  }

  UseRawFileData();
  if (!parInstrSet)
    pRoot->AddVGMFile(this);
  bLoaded = true;
  return true;
}


bool VGMSampColl::GetHeaderInfo() {
  return true;
}

bool VGMSampColl::GetSampleInfo() {
  return true;
}

VGMSamp *VGMSampColl::AddSamp(uint32_t offset, uint32_t length, uint32_t dataOffset, uint32_t dataLength,
                              uint8_t nChannels, uint16_t bps, uint32_t theRate, wstring name) {
  VGMSamp *newSamp = new VGMSamp(this, offset, length, dataOffset, dataLength, nChannels,
                                 bps, theRate, name);
  samples.push_back(newSamp);
  return newSamp;
}

bool VGMSampColl::OnSaveAllAsWav() {
  wstring dirpath = pRoot->UI_GetSaveDirPath();
  if (dirpath.length() != 0) {
    for (uint32_t i = 0; i < samples.size(); i++) {
      wstring filepath = dirpath + L"\\" + ConvertToSafeFileName(samples[i]->sampName) + L".wav";
      samples[i]->SaveAsWav(filepath);
    }
    return true;
  }
  return false;
}
