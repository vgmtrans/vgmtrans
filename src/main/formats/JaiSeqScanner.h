#pragma once
#include "Scanner.h"

class JaiSeqScanner :
	public VGMScanner {
public:
	JaiSeqScanner(void) {
		USE_EXTENSION(L"bms")
	}
	virtual ~JaiSeqScanner(void) {
	}

	virtual void Scan(RawFile *file, void *info = 0);
};
