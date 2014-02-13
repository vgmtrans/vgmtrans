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
	VGMMiscFile(const std::string& format, RawFile* file, ULONG offset, ULONG length = 0, std::wstring name = L"VGMMiscFile");

	virtual FileType GetFileType() { return FILETYPE_MISC; }

	virtual bool LoadMain();
	virtual bool Load();
};

