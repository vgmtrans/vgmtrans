/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include "FileLoader.h"

class SPC2Loader : public FileLoader {
public:
  SPC2Loader() = default;
  ~SPC2Loader() override = default;
  void apply(const RawFile *) override;
};

