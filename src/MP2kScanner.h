#pragma once
#include "Scanner.h"

class MP2kScanner :
	public VGMScanner
{
public:
	MP2kScanner(void);
public:
	virtual ~MP2kScanner(void);

	virtual void Scan(RawFile* file, void* info = 0);
};
