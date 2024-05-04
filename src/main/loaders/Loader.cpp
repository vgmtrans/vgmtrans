
#include "Loader.h"

VGMLoader::VGMLoader(void) {
}

VGMLoader::~VGMLoader(void) {
}

PostLoadCommand VGMLoader::Apply(RawFile *theFile) {
  return KEEP_IT;
}