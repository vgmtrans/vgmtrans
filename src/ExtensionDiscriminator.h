#pragma once

#include "common.h"

class VGMScanner;

class ExtensionDiscriminator
{
public:
	ExtensionDiscriminator(void);
	~ExtensionDiscriminator(void);

	int AddExtensionScannerAssoc(wstring extension, VGMScanner*);
	list<VGMScanner*>* GetScannerList(wstring extension);

	map<wstring, list<VGMScanner*> > mScannerExt;
	static ExtensionDiscriminator& instance()
	{
		static ExtensionDiscriminator instance ;
		return instance;
	}
};

