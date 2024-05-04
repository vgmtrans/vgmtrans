/**
* VGMTrans (c) - 2002-2023
* Licensed under the zlib license
* See the included LICENSE for more information
*/

#pragma once

#include "FileLoader.h"

class RSNLoader: public FileLoader {
public:
  RSNLoader() = default;
  void apply(const RawFile *theFile) override;

};
