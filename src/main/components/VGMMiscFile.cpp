/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMMiscFile.h"
#include "Root.h"
#include "Format.h"

// ***********
// VGMMiscFile
// ***********

VGMMiscFile::VGMMiscFile(const std::string &format, RawFile *file, uint32_t offset, uint32_t length,
                         std::string name)
    : VGMFile(format, file, offset, length, std::move(name)) {
}

bool VGMMiscFile::loadMain() {
  return true;
}

bool VGMMiscFile::loadVGMFile(bool useMatcher) {
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

bool VGMMiscFile::load() {
  if (!loadMain()) {
    return false;
  }
  if (length() == 0) {
    return false;
  }

  rawFile()->addContainedVGMFile(std::make_shared<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>>(this));
  pRoot->addVGMFile(this);
  return true;
}
