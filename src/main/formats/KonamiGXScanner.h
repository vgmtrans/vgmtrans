#pragma once
#include "../Scanner.h"

class KonamiGXScanner :
	public VGMScanner
{
public:
	virtual void Scan(RawFile* file, void* info = 0);
	void LoadSeqTable(RawFile* file, uint32_t offset);
};



