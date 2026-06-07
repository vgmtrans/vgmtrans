/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "VGMSampColl.h"

#include "base/Types.h"
#include "Format.h"
#include "Helper.h"
#include "Root.h"
#include "VGMSamp.h"

// ***********
// VGMSampColl
// ***********

VGMSampColl::VGMSampColl(const std::string &format, RawFile *rawfile, u32 offset, u32 length,
                         std::string name)
    : VGMFile(format, rawfile, offset, length, std::move(name)),
      sampDataOffset(0),
      parInstrSet(nullptr),
      m_should_load_on_instr_set_match(false),
      bLoaded(false) {
}

VGMSampColl::~VGMSampColl() = default;

VGMSampColl::VGMSampColl(const std::string &format, RawFile *rawfile, VGMInstrSet *instrset,
                         u32 offset, u32 length, std::string name)
    : VGMFile(format, rawfile, offset, length, std::move(name)),
      sampDataOffset(0),
      parInstrSet(instrset),
      m_should_load_on_instr_set_match(false),
      bLoaded(false) {
}

bool VGMSampColl::load() {
  if (bLoaded)
    return true;
  if (!parseHeader())
    return false;
  if (!parseSampleInfo()) {
    clearSamples();
    return false;
  }

  if (m_samples.empty()) {
    clearSamples();
    return false;
  }

  for (auto& sample : m_ownedSamples) {
    sinkChild(std::move(sample));
  }
  m_ownedSamples.clear();

  if (length() == 0) {
    for (auto* samp : m_samples) {
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
                              u32 rate, std::string name) {
  return addSamp<VGMSamp>(this, offset, length, dataOffset, dataLength, nChannels, bps, rate, std::move(name));
}

VGMSamp *VGMSampColl::sinkSamp(std::unique_ptr<VGMSamp>&& samp) {
  auto* rawSamp = samp.get();
  m_samples.emplace_back(rawSamp);
  m_ownedSamples.emplace_back(std::move(samp));
  return rawSamp;
}

void VGMSampColl::clearSamples() {
  m_samples.clear();
  m_ownedSamples.clear();
}
