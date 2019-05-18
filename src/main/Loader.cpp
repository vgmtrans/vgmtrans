/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 






#include "Loader.h"

VGMLoader::VGMLoader(void) {
}

VGMLoader::~VGMLoader(void) {
}

PostLoadCommand VGMLoader::Apply(RawFile *theFile) {
  return KEEP_IT;
}