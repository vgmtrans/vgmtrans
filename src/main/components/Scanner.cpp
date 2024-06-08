/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "Scanner.h"

bool VGMScanner::init() {
  return true;
}

void VGMScanner::initiateScan(RawFile *file, void *offset) {
  this->scan(file, offset);
}