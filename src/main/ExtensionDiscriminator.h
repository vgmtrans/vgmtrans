/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "common.h"

class VGMScanner;

class ExtensionDiscriminator {
   public:
    ExtensionDiscriminator(void);
    ~ExtensionDiscriminator(void);

    int AddExtensionScannerAssoc(std::wstring extension, VGMScanner *);
    std::list<VGMScanner *> *GetScannerList(std::wstring extension);

    std::map<std::wstring, std::list<VGMScanner *>> mScannerExt;
    static ExtensionDiscriminator &instance() {
        static ExtensionDiscriminator instance;
        return instance;
    }
};
