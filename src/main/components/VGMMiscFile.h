#pragma once
#include "common.h"
#include "VGMFile.h"

// ***********
// VGMMiscFile
// ***********

class RawFile;

class VGMMiscFile:
    public VGMFile {
 public:
  VGMMiscFile(const std::string &format,
              RawFile *file,
              uint32_t offset,
              uint32_t length = 0,
              std::string name = "VGMMiscFile");

  virtual FileType GetFileType() { return FILETYPE_MISC; }

  virtual bool LoadMain();
  virtual bool Load();
};

