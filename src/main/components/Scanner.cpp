/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "Scanner.h"

VGMScanner::VGMScanner(Format *format): m_format(format) {}

bool VGMScanner::init() {
  return true;
}
