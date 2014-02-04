#pragma once
#include "Scanner.h"

class AkaoScanner :
	public VGMScanner
{
public:
	AkaoScanner(void);
public:
	virtual ~AkaoScanner(void);

	virtual void Scan(RawFile* file, void* info = 0);
};
