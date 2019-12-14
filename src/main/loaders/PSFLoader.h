/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "FileLoader.h"
#include "LoaderManager.h"

class PSFFile2;

class PSFLoader : public FileLoader {
   public:
    ~PSFLoader() = default;
    void apply(const RawFile *) override;

   private:
    void psf_read_exe(const RawFile *file, int version);
};

namespace vgmtrans::loaders {
LoaderRegistration<PSFLoader> psf{"PSF"};
}
