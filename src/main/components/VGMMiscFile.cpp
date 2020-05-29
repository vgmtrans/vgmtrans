/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMMiscFile.h"

#include <utility>
#include "Root.h"
#include "Format.h"

// ***********
// VGMMiscFile
// ***********

VGMMiscFile::VGMMiscFile(const std::string &format, RawFile *file, uint32_t offset, uint32_t length,
                         std::string name)
    : VGMFile(format, file, offset, length, std::move(name)) {}

bool VGMMiscFile::LoadMain() {
    return true;
}

bool VGMMiscFile::LoadVGMFile() {
    bool val = Load();
    if (!val) {
        return false;
    }

    if (auto fmt = GetFormat(); fmt) {
        fmt->OnNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>(this));
    }

    return val;
}

bool VGMMiscFile::Load() {
    if (!LoadMain()) {
        return false;
    }
    if (unLength == 0) {
        return false;
    }

    rawfile->AddContainedVGMFile(std::make_shared<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>>(this));
    pRoot->AddVGMFile(this);
    return true;
}
