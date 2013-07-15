#pragma once
#include "scanner.h"

class PS1SeqScanner :
	public VGMScanner
{
public:
	PS1SeqScanner(void);
	virtual ~PS1SeqScanner(void);

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForPS1Seq (RawFile* file);
	void SearchForVab (RawFile* file);
};
