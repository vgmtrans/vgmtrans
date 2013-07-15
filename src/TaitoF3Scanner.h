#pragma once
#include "Scanner.h"

class TaitoF3Scanner :
	public VGMScanner
{
public:
	virtual void Scan(RawFile* file, void* info = 0);
	//void LoadSeqTable(RawFile* file, UINT offset);
};
