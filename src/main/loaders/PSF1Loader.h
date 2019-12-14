/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "FileLoader.h"
#include "LoaderManager.h"

class PSFFile2;

class PSF1Loader : public FileLoader {
   public:
    ~PSF1Loader() = default;
    void apply(const RawFile *) override;

   private:
    void psf_read_exe(const RawFile *file);
};

namespace vgmtrans::loaders {
LoaderRegistration<PSF1Loader> _psf1{"PSF1"};
}
