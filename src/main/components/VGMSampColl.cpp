/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "VGMSampColl.h"

#include "VGMSamp.h"
#include "Root.h"
#include "Format.h"
#include "helper.h"

// ***********
// VGMSampColl
// ***********

VGMSampColl::VGMSampColl(const std::string &format, RawFile *rawfile, uint32_t offset, uint32_t length,
                         std::string theName)
    : VGMFile(format, rawfile, offset, length, std::move(theName)),
      bLoadOnInstrSetMatch(false),
      bLoaded(false),
      sampDataOffset(0),
      parInstrSet(nullptr) {
  AddContainer<VGMSamp>(samples);
}

VGMSampColl::VGMSampColl(const std::string &format, RawFile *rawfile, VGMInstrSet *instrset,
                         uint32_t offset, uint32_t length, std::string theName)
    : VGMFile(format, rawfile, offset, length, std::move(theName)),
      bLoadOnInstrSetMatch(false),
      bLoaded(false),
      sampDataOffset(0),
      parInstrSet(instrset) {
  AddContainer<VGMSamp>(samples);
}

VGMSampColl::~VGMSampColl() {
  DeleteVect<VGMSamp>(samples);
}

bool VGMSampColl::LoadVGMFile() {
  bool val = Load();
  if (!val) {
    return false;
  }

  if (auto fmt = GetFormat(); fmt) {
    fmt->OnNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>(this));
  }

  return val;
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
      VGMSamp *samp = *itr;

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

  if (!parInstrSet) {
    rawfile->AddContainedVGMFile(std::make_shared<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>>(this));
    pRoot->AddVGMFile(this);
  }

  bLoaded = true;
  return true;
}

bool VGMSampColl::GetHeaderInfo() {
  return true;
}

bool VGMSampColl::GetSampleInfo() {
  return true;
}

VGMSamp *VGMSampColl::AddSamp(uint32_t offset, uint32_t length, uint32_t dataOffset,
                              uint32_t dataLength, uint8_t nChannels, uint16_t bps,
                              uint32_t theRate, std::string name) {
  VGMSamp *newSamp = new VGMSamp(this, offset, length, dataOffset, dataLength, nChannels, bps, theRate, std::move(name));
  samples.push_back(newSamp);
  return newSamp;
}
