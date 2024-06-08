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
      m_should_load_on_instr_set_match(false),
      bLoaded(false),
      sampDataOffset(0),
      parInstrSet(nullptr) {
}

VGMSampColl::VGMSampColl(const std::string &format, RawFile *rawfile, VGMInstrSet *instrset,
                         uint32_t offset, uint32_t length, std::string theName)
    : VGMFile(format, rawfile, offset, length, std::move(theName)),
      m_should_load_on_instr_set_match(false),
      bLoaded(false),
      sampDataOffset(0),
      parInstrSet(instrset) {
}

bool VGMSampColl::loadVGMFile() {
  bool val = load();
  if (!val) {
    return false;
  }

  if (auto fmt = format(); fmt) {
    fmt->onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>(this));
  }

  return val;
}


bool VGMSampColl::load() {
  if (bLoaded)
    return true;
  if (!parseHeader())
    return false;
  if (!parseSampleInfo())
    return false;

  if (samples.size() == 0)
    return false;

  addChildren(samples);

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
    rawFile()->addContainedVGMFile(std::make_shared<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>>(this));
    pRoot->addVGMFile(this);
  }

  bLoaded = true;
  return true;
}

bool VGMSampColl::parseHeader() {
  return true;
}

bool VGMSampColl::parseSampleInfo() {
  return true;
}

VGMSamp *VGMSampColl::addSamp(uint32_t offset, uint32_t length, uint32_t dataOffset,
                              uint32_t dataLength, uint8_t nChannels, uint16_t bps,
                              uint32_t theRate, std::string name) {
  VGMSamp *newSamp = new VGMSamp(this, offset, length, dataOffset, dataLength, nChannels, bps, theRate, std::move(name));
  samples.push_back(newSamp);
  return newSamp;
}
