/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "FileLoader.h"

class CHDLoader : public FileLoader {
 public:
  ~CHDLoader() override = default;
  void apply(const RawFile *) override;
};