#pragma once
#include "Loader.h"
#include "PSFFile.h"

class SNSFLoader:
    public VGMLoader {
 public:
  SNSFLoader(void);
 public:
  virtual ~SNSFLoader(void);

  virtual PostLoadCommand Apply(RawFile *theFile);
  const char* psf_read_exe(RawFile *file, unsigned char *&exebuffer, size_t &exebuffersize);
 private:
  const char* psf_read_exe_sub
      (RawFile *file, unsigned char *&exebuffer, size_t &exebuffersize, uint32_t &base_offset, bool &base_set);
  const char* load_psf_libs(PSFFile &psf,
                               RawFile *file,
                               unsigned char *&exebuffer,
                               size_t &exebuffersize,
                               uint32_t &base_offset,
                               bool &base_set);
};
