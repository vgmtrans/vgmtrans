/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "FileLoader.h"
#include "LoaderManager.h"

class PSFFile2;

class NDS2SFLoader : public FileLoader {
   public:
    ~NDS2SFLoader() = default;
    void apply(const RawFile *) override;

   private:
    void psf_read_exe(const RawFile *file);
};

namespace vgmtrans::loaders {
LoaderRegistration<NDS2SFLoader> _nds2sf{"NDS_2SF"};
}
