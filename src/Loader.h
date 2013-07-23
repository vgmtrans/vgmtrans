#pragma once

#include "RawFile.h"

enum PostLoadCommand {KEEP_IT, DELETE_IT};

class VGMLoader
{
public:
	VGMLoader(void);
public:
	virtual ~VGMLoader(void);

	virtual PostLoadCommand Apply(RawFile* theFile);
};
