#pragma once
#include "Scanner.h"

class SonyPS2Scanner :
	public VGMScanner
{
public:
	SonyPS2Scanner(void)
	{
		USE_EXTENSION(L"sq")
		USE_EXTENSION(L"hd")
		USE_EXTENSION(L"bd")
	}
public:
	~SonyPS2Scanner(void)
	{
	}
	
	//virtual bool UseExtension()
	//{
	//	theExtensionDiscriminator.AddExtensionScannerAssoc(L"sq", this);
	//	theExtensionDiscriminator.AddExtensionScannerAssoc(L"hd", this);
	//	theExtensionDiscriminator.AddExtensionScannerAssoc(L"bd", this);
	//	return true;
	//}
	//USE_EXTENSION(L"sq")
	//USE_EXTENSION(L"hd")
	//USE_EXTENSION(L"bd")

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForSeq (RawFile* file);
	void SearchForInstrSet (RawFile* file);
	void SearchForSampColl (RawFile* file);
	//void SearchForWDSet (RawFile* file);
};
