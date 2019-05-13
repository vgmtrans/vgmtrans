/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Loader.h"
#include "PSFFile.h"

class GSFLoader:
    public VGMLoader {
 public:
  GSFLoader(void);
 public:
  virtual ~GSFLoader(void);

  virtual PostLoadCommand Apply(RawFile *theFile);
  const wchar_t *psf_read_exe(RawFile *file, unsigned char *&exebuffer, size_t &exebuffersize);
 private:
  const wchar_t *load_psf_libs(PSFFile &psf, RawFile *file, unsigned char *&exebuffer, size_t &exebuffersize);
};
