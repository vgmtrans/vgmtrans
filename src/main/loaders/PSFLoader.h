/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "components/FileLoader.h"

class PSFFile;

class PSFLoader : public FileLoader {
   public:
    ~PSFLoader() override = default;
    void apply(const RawFile *) override;

   private:
    void psf_read_exe(const RawFile *file, int version);
};
