/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <list>
#include <map>

class VGMScanner;

class ExtensionDiscriminator {
 public:
  ExtensionDiscriminator();
  ~ExtensionDiscriminator();

  int AddExtensionScannerAssoc(const std::string& extension, VGMScanner *);
  std::list<VGMScanner *> *GetScannerList(const std::string& extension);

  std::map<std::string, std::list<VGMScanner *> > mScannerExt;
  static ExtensionDiscriminator &instance() {
    static ExtensionDiscriminator instance;
    return instance;
  }
};

