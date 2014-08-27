#pragma once
#include "../Scanner.h"

class FFTScanner :
	public VGMScanner
{
public:
	FFTScanner(void);
public:
	virtual ~FFTScanner(void);

	virtual void Scan(RawFile* file, void* info = 0);	//scan "smds" and "wds"
	void SearchForFFTSeq (RawFile* file);				//scan "smds"
	void SearchForFFTwds (RawFile* file);				//scan "wds"

};



