/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "components/FileLoader.h"

class SPCLoader : public FileLoader {
 public:
    ~SPCLoader() override = default;
    void apply(const RawFile *) override;
};
