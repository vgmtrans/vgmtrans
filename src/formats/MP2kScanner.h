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

protected:
	bool Mp2kDetector(RawFile* file, uint32_t & mp2k_offset);
};
