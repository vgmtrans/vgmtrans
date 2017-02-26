#pragma once
#include "Scanner.h"

class RSARScanner :
	public VGMScanner {
public:
	RSARScanner(void) {
		USE_EXTENSION(L"brsar")
	}
	virtual ~RSARScanner(void) {
	}

	virtual void Scan(RawFile *file, void *info = 0);
};
