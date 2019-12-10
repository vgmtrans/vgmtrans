/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Loader.h"
#include "PSFFile2.h"

class PSF1Loader : public VGMLoader {
   public:
    PostLoadCommand Apply(RawFile *) override;

   private:
    std::vector<char> *psf_read_exe(RawFile *file);
    bool load_psf_libs(PSFFile2 &psf, RawFile *file);
};
