/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "loaders/FileLoader.h"
#include "loaders/LoaderManager.h"

class SPCLoader : public FileLoader {
   public:
    ~SPCLoader() = default;
    void apply(const RawFile *) override;
};

namespace vgmtrans::loaders {
LoaderRegistration<SPCLoader> _spc{"SPC"};
}
