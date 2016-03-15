#pragma once

#include "common.h"

class VGMScanner;

class ExtensionDiscriminator {
 public:
  ExtensionDiscriminator(void);
  ~ExtensionDiscriminator(void);

  int AddExtensionScannerAssoc(std::wstring extension, VGMScanner *);
  std::list<VGMScanner *> *GetScannerList(std::wstring extension);

  std::map<std::wstring, std::list<VGMScanner *> > mScannerExt;
  static ExtensionDiscriminator &instance() {
    static ExtensionDiscriminator instance;
    return instance;
  }
};

