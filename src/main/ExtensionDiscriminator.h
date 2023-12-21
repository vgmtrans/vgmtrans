#pragma once

#include "common.h"

class VGMScanner;

class ExtensionDiscriminator {
 public:
  ExtensionDiscriminator(void);
  ~ExtensionDiscriminator(void);

  int AddExtensionScannerAssoc(std::string extension, VGMScanner *);
  std::list<VGMScanner *> *GetScannerList(std::string extension);

  std::map<std::string, std::list<VGMScanner *> > mScannerExt;
  static ExtensionDiscriminator &instance() {
    static ExtensionDiscriminator instance;
    return instance;
  }
};

