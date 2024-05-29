/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "Scanner.h"

bool VGMScanner::Init() {
  return true;
}

void VGMScanner::InitiateScan(RawFile *file, void *offset) {
  this->Scan(file, offset);
}