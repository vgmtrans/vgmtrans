#include "stdafx.h"
#include "ExtensionDiscriminator.h"

//static ExtensionDiscriminator theExtensionDiscriminator;
//ExtensionDiscriminator ExtensionDiscriminator::instance;

ExtensionDiscriminator::ExtensionDiscriminator(void)
{
}

ExtensionDiscriminator::~ExtensionDiscriminator(void)
{
}


int ExtensionDiscriminator::AddExtensionScannerAssoc(std::wstring extension, VGMScanner* scanner)
{
//	if (mScannerExt.find(extension) == mScannerExt.end())
		mScannerExt[extension].push_back(scanner);
		return true;
}

std::list<VGMScanner*>* ExtensionDiscriminator::GetScannerList(std::wstring extension)
{
	std::map<std::wstring, std::list<VGMScanner*> >::iterator iter = mScannerExt.find(StringToLower(extension));
	if (iter == mScannerExt.end())
		return NULL;
	else
		return &(*iter).second;
}
