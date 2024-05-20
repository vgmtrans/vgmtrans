
#include "Loader.h"

VGMLoader::VGMLoader() {
}

VGMLoader::~VGMLoader() {
}

PostLoadCommand VGMLoader::Apply(RawFile *theFile) {
  return KEEP_IT;
}