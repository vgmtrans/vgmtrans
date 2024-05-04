/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "common.h"
#include <list>
#include <map>

class VGMScanner;

class ExtensionDiscriminator {
 public:
  ExtensionDiscriminator();
  ~ExtensionDiscriminator();

  int AddExtensionScannerAssoc(std::string extension, VGMScanner *);
  std::list<VGMScanner *> *GetScannerList(std::string extension);

  std::map<std::string, std::list<VGMScanner *> > mScannerExt;
  static ExtensionDiscriminator &instance() {
    static ExtensionDiscriminator instance;
    return instance;
  }
};

