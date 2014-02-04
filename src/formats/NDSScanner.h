#pragma once
#include "Scanner.h"

class NDSScanner :
	public VGMScanner
{
public:
	NDSScanner(void)
	{
		USE_EXTENSION(L"nds")
		USE_EXTENSION(L"sdat")
	}
	virtual ~NDSScanner(void)
	{
	}
	

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForSDAT (RawFile* file);
	ULONG LoadFromSDAT(RawFile* file, ULONG offset);
};
