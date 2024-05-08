#pragma once

#include "RawFile.h"

enum PostLoadCommand { KEEP_IT, DELETE_IT };

class VGMLoader {
 public:
  VGMLoader();
  virtual ~VGMLoader();

  virtual PostLoadCommand Apply(RawFile *theFile);
};
