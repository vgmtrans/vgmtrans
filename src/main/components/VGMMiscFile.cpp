/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMMiscFile.h"

#include "base/Types.h"
#include "Format.h"
#include "Root.h"

// ***********
// VGMMiscFile
// ***********

VGMMiscFile::VGMMiscFile(const std::string &format, RawFile *file, u32 offset, u32 length,
                         std::string name)
    : VGMFile(format, file, offset, length, std::move(name)) {
}

bool VGMMiscFile::loadMain() {
  return true;
}

bool VGMMiscFile::load() {
  if (!loadMain()) {
    return false;
  }
  if (length() == 0) {
    return false;
  }

  return true;
}
