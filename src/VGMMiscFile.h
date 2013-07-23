#pragma once
#include "common.h"
#include "VGMFile.h"
//#include "Menu.h"

// ***********
// VGMMiscFile
// ***********

class RawFile;

class VGMMiscFile :
	public VGMFile
{
public:
	VGMMiscFile(const string& format, RawFile* file, ULONG offset, ULONG length = 0, wstring name = L"VGMMiscFile");

	virtual FileType GetFileType() { return FILETYPE_MISC; }

	virtual bool LoadMain();
	virtual bool Load();
};

