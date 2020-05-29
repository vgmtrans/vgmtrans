/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "common.h"
#include "VGMFile.h"
//#include "Menu.h"

// ***********
// VGMMiscFile
// ***********

class RawFile;

class VGMMiscFile : public VGMFile {
   public:
    VGMMiscFile(const std::string &format, RawFile *file, uint32_t offset, uint32_t length = 0,
                std::string name = "VGMMiscFile");

    bool LoadVGMFile() override;
    virtual bool LoadMain();
    virtual bool Load();
};
