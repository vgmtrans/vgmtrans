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
	VGMMiscFile(const std::string& format, RawFile* file, uint32_t offset, uint32_t length = 0, std::wstring name = L"VGMMiscFile");

	virtual FileType GetFileType() { return FILETYPE_MISC; }

	virtual bool LoadMain();
	virtual bool Load();
};

