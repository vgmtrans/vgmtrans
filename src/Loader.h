#pragma once

#include "RawFile.h"

enum {KEEP_IT, DELETE_IT};

class VGMLoader
{
public:
	VGMLoader(void);
public:
	virtual ~VGMLoader(void);

	virtual int Apply(RawFile* theFile);
};
