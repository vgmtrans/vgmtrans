/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "common.h"
#include "ExtensionDiscriminator.h"

ExtensionDiscriminator::ExtensionDiscriminator() {
}

ExtensionDiscriminator::~ExtensionDiscriminator() {
}


int ExtensionDiscriminator::AddExtensionScannerAssoc(const std::string& extension, VGMScanner *scanner) {
  mScannerExt[extension].push_back(scanner);
  return true;
}

std::list<VGMScanner *> *ExtensionDiscriminator::GetScannerList(const std::string& extension) {
  std::map<std::string, std::list<VGMScanner *> >::iterator iter = mScannerExt.find(toLower(extension));
  if (iter == mScannerExt.end())
    return NULL;
  else
    return &(*iter).second;
}
