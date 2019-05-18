/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 

#include <cassert>
#include <cwchar>
#include <cmath>
#include <algorithm>
#include <climits>
#include <stdio.h>
#include <cstdint>

#include <fstream>
#include <vector>
#include <list>
#include <map>
#include <string>
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <iterator>
#include <iostream>
#include <iomanip>
#include <ctype.h>
#include "portable.h"
#define countof(arr) sizeof(arr) / sizeof(arr[0])


#include "ExtensionDiscriminator.h"

//static ExtensionDiscriminator theExtensionDiscriminator;
//ExtensionDiscriminator ExtensionDiscriminator::instance;

ExtensionDiscriminator::ExtensionDiscriminator(void) {
}

ExtensionDiscriminator::~ExtensionDiscriminator(void) {
}


int ExtensionDiscriminator::AddExtensionScannerAssoc(std::wstring extension, VGMScanner *scanner) {
  mScannerExt[extension].push_back(scanner);
  return true;
}

std::list<VGMScanner *> *ExtensionDiscriminator::GetScannerList(std::wstring extension) {
  std::map<std::wstring, std::list<VGMScanner *> >::iterator iter = mScannerExt.find(StringToLower(extension));
  if (iter == mScannerExt.end())
    return NULL;
  else
    return &(*iter).second;
}
