#pragma once
#include "Loader.h"

class SPCLoader :
	public VGMLoader
{
public:
	SPCLoader(void);
public:
	virtual ~SPCLoader(void);

	virtual PostLoadCommand Apply(RawFile* theFile);
};