/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "components/FileLoader.h"
#include "LoaderManager.h"

class PSF2Loader final : public FileLoader {
   public:
    ~PSF2Loader() = default;
    void apply(const RawFile *) override;

   private:
    int psf2_decompress_block(const RawFile *file, unsigned fileoffset, unsigned blocknumber,
                              unsigned numblocks, unsigned char *decompressedblock,
                              unsigned blocksize);
    int psf2unpack(const RawFile *file, unsigned long fileoffset, unsigned long dircount);
};

namespace vgmtrans::loaders {
LoaderRegistration<PSF2Loader> _psf2("PSF2");
}
