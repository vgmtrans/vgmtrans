/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "components/FileLoader.h"

class NitroFsLoader final : public FileLoader {
public:
  void apply(const RawFile* file) override;
};
