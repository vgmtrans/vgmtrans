/**
* VGMTrans (c) - 2002-2023
* Licensed under the zlib license
* See the included LICENSE for more information
*/

#pragma once

#include "Loader.h"

class RSNLoader:
    public VGMLoader {
public:
  RSNLoader(void) = default;
public:
  virtual ~RSNLoader(void) = default;
  virtual PostLoadCommand Apply(RawFile *theFile);
};
