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



#include "Scanner.h"
#include "Root.h"

VGMScanner::VGMScanner() {
}

VGMScanner::~VGMScanner(void) {
}

bool VGMScanner::Init() {
  //if (!UseExtension())
  //	return false;
  return true;
}

void VGMScanner::InitiateScan(RawFile *file, void *offset) {
  pRoot->UI_SetScanInfo();
  this->Scan(file, offset);
}


//void VGMScanner::Scan(RawFile* file)
//{
//	return;
//}