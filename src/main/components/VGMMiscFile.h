/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"
#include "VGMFile.h"

#include <string>

// ***********
// VGMMiscFile
// ***********

class RawFile;

class VGMMiscFile : public VGMFile {
public:
  VGMMiscFile(const std::string &format, RawFile *file, u32 offset, u32 length = 0,
              std::string name = "VGMMiscFile");

  virtual bool loadMain();
  bool load() override;
};
