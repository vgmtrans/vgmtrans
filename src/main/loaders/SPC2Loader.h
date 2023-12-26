/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include "Loader.h"

class SPC2Loader:
    public VGMLoader {
public:
  SPC2Loader(void) = default;
public:
  virtual ~SPC2Loader(void) = default;
  virtual PostLoadCommand Apply(RawFile *theFile);
};

