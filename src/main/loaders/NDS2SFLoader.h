/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Loader.h"
#include "PSFFile.h"

class NDS2SFLoader : public VGMLoader {
   public:
    NDS2SFLoader(void);

   public:
    virtual ~NDS2SFLoader(void);

    virtual PostLoadCommand Apply(RawFile *theFile);
    const char *psf_read_exe(RawFile *file, unsigned char *&exebuffer, size_t &exebuffersize);

   private:
    const char *load_psf_libs(PSFFile &psf, RawFile *file, unsigned char *&exebuffer,
                                 size_t &exebuffersize);
};
