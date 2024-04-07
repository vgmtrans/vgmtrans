#pragma once
#include "Scanner.h"

class MP2kScanner:
    public VGMScanner {
 public:
  MP2kScanner(void);
 public:
  virtual ~MP2kScanner(void);

  virtual void Scan(RawFile *file, void *info = 0);

 protected:
  bool Mp2kDetector(RawFile *file, uint32_t &mp2k_offset);

 private:
  static bool IsValidOffset(uint32_t offset, uint32_t romsize);
  static bool IsGBAROMAddress(uint32_t address);
  static uint32_t GBAAddressToOffset(uint32_t address);
  static off_t LooseSearch
      (RawFile *file, const uint8_t *src, size_t srcsize, off_t file_offset, size_t alignment, int diff_threshold);
};
