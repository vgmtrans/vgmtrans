/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Loader.h"

class SPCLoader : public VGMLoader {
   public:
    SPCLoader(void);

   public:
    virtual ~SPCLoader(void);

    virtual PostLoadCommand Apply(RawFile *theFile);
};