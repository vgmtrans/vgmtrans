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


#include "VGMMiscFile.h"
#include "Root.h"

using namespace std;

// ***********
// VGMMiscFile
// ***********

VGMMiscFile::VGMMiscFile(const string &format, RawFile *file, uint32_t offset, uint32_t length, wstring name)
    : VGMFile(FILETYPE_MISC, format, file, offset, length, name) {

}

bool VGMMiscFile::LoadMain() {
  return true;
}

bool VGMMiscFile::Load() {
  if (!LoadMain())
    return false;
  if (unLength == 0)
    return false;

  LoadLocalData();
  UseLocalData();
  pRoot->AddVGMFile(this);
  return true;
}
