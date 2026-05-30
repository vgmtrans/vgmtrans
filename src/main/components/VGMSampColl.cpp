/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "VGMSampColl.h"

#include "base/Types.h"
#include "Format.h"
#include "Root.h"
#include "VGMSamp.h"

// ***********
// VGMSampColl
// ***********

VGMSampColl::VGMSampColl(const std::string &format, RawFile *rawfile, u32 offset, u32 length,
                         std::string theName)
    : VGMFile(format, rawfile, offset, length, std::move(theName)),
      sampDataOffset(0),
      parInstrSet(nullptr),
      m_should_load_on_instr_set_match(false),
      bLoaded(false) {
}

VGMSampColl::VGMSampColl(const std::string &format, RawFile *rawfile, VGMInstrSet *instrset,
                         u32 offset, u32 length, std::string theName)
    : VGMFile(format, rawfile, offset, length, std::move(theName)),
      sampDataOffset(0),
      parInstrSet(instrset),
      m_should_load_on_instr_set_match(false),
      bLoaded(false) {
}

bool VGMSampColl::loadVGMFile(bool useMatcher) {
  bool val = load();
  if (!val) {
    return false;
  }

  if (useMatcher) {
    if (auto fmt = format(); fmt) {
      fmt->onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>(this));
    }
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

  if (length() == 0) {
    for (std::vector<VGMSamp *>::iterator itr = samples.begin(); itr != samples.end(); ++itr) {
      VGMSamp *samp = *itr;

      // Some formats can have negative sample offset
      // For example, Konami's SNES format and Hudson's SNES format
      // TODO: Fix negative sample offset without breaking instrument
      //assert(offset() <= samp->offset());

      //if (offset() > samp->offset())
      //{
      //	setLength(length() + (samp->offset() - offset()));
      //	setOffset(samp->offset());
      //}

      if (offset() + length() < samp->offset() + samp->length()) {
        setLength((samp->offset() + samp->length()) - offset());
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

VGMSamp *VGMSampColl::addSamp(u32 offset, u32 length, u32 dataOffset,
                              u32 dataLength, u8 nChannels, BPS bps,
                              u32 theRate, std::string name) {
  VGMSamp *newSamp = new VGMSamp(this, offset, length, dataOffset, dataLength, nChannels, bps, theRate, std::move(name));
  samples.push_back(newSamp);
  return newSamp;
}
