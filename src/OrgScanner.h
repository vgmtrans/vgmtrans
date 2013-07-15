#pragma once
#include "scanner.h"

class OrgScanner :
	public VGMScanner
{
public:
	OrgScanner(void);
	virtual ~OrgScanner(void);

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForOrgSeq (RawFile* file);
};
