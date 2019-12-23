/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ExtensionDiscriminator.h"

// static ExtensionDiscriminator theExtensionDiscriminator;
// ExtensionDiscriminator ExtensionDiscriminator::instance;

ExtensionDiscriminator::ExtensionDiscriminator(void) {}

ExtensionDiscriminator::~ExtensionDiscriminator(void) {}

int ExtensionDiscriminator::AddExtensionScannerAssoc(std::string extension, VGMScanner *scanner) {
    mScannerExt[extension].push_back(scanner);
    return true;
}

std::list<VGMScanner *> *ExtensionDiscriminator::GetScannerList(std::string extension) {
    std::map<std::string, std::list<VGMScanner *>>::iterator iter =
        mScannerExt.find(StringToLower(extension));
    if (iter == mScannerExt.end())
        return NULL;
    else
        return &(*iter).second;
}
