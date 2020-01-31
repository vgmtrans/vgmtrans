/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMMiscFile.h"
#include "Root.h"

using namespace std;

// ***********
// VGMMiscFile
// ***********

VGMMiscFile::VGMMiscFile(const string &format, RawFile *file, uint32_t offset, uint32_t length,
                         string name)
    : VGMFile(FILETYPE_MISC, format, file, offset, length, name) {}

bool VGMMiscFile::LoadMain() {
    return true;
}

bool VGMMiscFile::Load() {
    if (!LoadMain()) {
        return false;
    }
    if (unLength == 0) {
        return false;
    }
    pRoot->AddVGMFile(this);
    return true;
}
