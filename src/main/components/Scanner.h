/*
 * VGMTrans (c) 2002-2020
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

class RawFile;

class VGMScanner {
   public:
    VGMScanner() = default;
    virtual ~VGMScanner() = default;

    virtual bool Init();
    void InitiateScan(RawFile *file, void *offset = 0);
    virtual void Scan(RawFile *file, void *offset = 0) = 0;
};
