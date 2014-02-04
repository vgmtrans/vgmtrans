#pragma once
#include "Scanner.h"

class SquarePS2Scanner :
	public VGMScanner
{
public:
	SquarePS2Scanner(void);
public:
	~SquarePS2Scanner(void);

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForBGMSeq (RawFile* file);
	void SearchForWDSet (RawFile* file);
};

