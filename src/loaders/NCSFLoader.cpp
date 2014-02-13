#include "stdafx.h"
#include "PSFFile.h"
#include "NCSFLoader.h"
#include "Root.h"
#include <zlib.h>

using namespace std;

#define NCSF_VERSION	0x25
#define NCSF_MAX_ROM_SIZE	0x10000000

wchar_t *GetFileWithBase(const wchar_t *f, const wchar_t *newfile);

NCSFLoader::NCSFLoader(void)
{
}

NCSFLoader::~NCSFLoader(void)
{
}

PostLoadCommand NCSFLoader::Apply(RawFile* file)
{
	BYTE sig[4];
	file->GetBytes(0, 4, sig);
	if (memcmp(sig, "PSF", 3) == 0)
	{
		BYTE version = sig[3];
		if (version == NCSF_VERSION)
		{
			const wchar_t *complaint;
			size_t exebufsize = NCSF_MAX_ROM_SIZE;
			BYTE* exebuf = NULL;
			//memset(exebuf, 0, exebufsize);

			complaint = psf_read_exe(file, exebuf, exebufsize);
			if(complaint) 
			{ 
				Alert(complaint);
				delete[] exebuf;
				return KEEP_IT; 
			}
			//pRoot->UI_WriteBufferToFile(L"uncomp.sdat", exebuf, exebufsize);

			wstring str = file->GetFileName();
			pRoot->CreateVirtFile(exebuf, exebufsize, str.data());
			return DELETE_IT;
		}
	}

	return KEEP_IT;
}




// The code below was written by Neill Corlett and adapted for VGMTrans.
// Recursive psflib loading has been added.


/***************************************************************************/
/*
** Read the EXE from a PSF file
**
** Returns the error message, or NULL on success
*/
const wchar_t* NCSFLoader::psf_read_exe(
  RawFile* file,
  unsigned char*& exebuffer,
  size_t& exebuffersize
) 
{
	PSFFile psf;
	if (!psf.Load(file))
		return psf.GetError();

	// search exclusively for _lib tag, and if found, perform a recursive load
	const wchar_t* psflibError = load_psf_libs(psf, file, exebuffer, exebuffersize);
	if (psflibError != NULL)
		return psflibError;

	DataSeg* ncsfExeHeadSeg;
	if (!psf.ReadExeDataSeg(ncsfExeHeadSeg, 0x0c, 0))
		return psf.GetError();

	uint32_t ncsfRomStart = 0;
	uint32_t ncsfRomSize = ncsfExeHeadSeg->GetWord(0x08);
	delete ncsfExeHeadSeg;
	if (ncsfRomStart + ncsfRomSize > exebuffersize || (exebuffer == NULL && exebuffersize == 0))
		return L"NCSF ROM section start and/or size values are likely corrupt.";

	if (exebuffer == NULL)
	{
		exebuffersize = ncsfRomStart + ncsfRomSize;
		exebuffer = new BYTE[exebuffersize];
		if (exebuffer == NULL)
		{
			return L"NCSF ROM memory allocation error.";
		}
		memset(exebuffer, 0, exebuffersize);
	}

	if (!psf.ReadExe(exebuffer + ncsfRomStart, ncsfRomSize, 0))
		return L"Decompression failed";

	return NULL;
}

const wchar_t* NCSFLoader::load_psf_libs(PSFFile& psf, RawFile* file, unsigned char*& exebuffer, size_t& exebuffersize)
{
	char libTagName[16];
	int libIndex = 1;
	while (true)
	{
		if (libIndex == 1)
			strcpy(libTagName, "_lib");
		else
			sprintf(libTagName, "_lib%d", libIndex);

		map<string, string>::iterator itLibTag = psf.tags.find(libTagName);
		if (itLibTag == psf.tags.end())
			break;

		wchar_t tempfn[_MAX_PATH] = { 0 };
		mbstowcs(tempfn, itLibTag->second.c_str(), itLibTag->second.size());

		wchar_t *fullPath;
		fullPath=GetFileWithBase(file->GetFullPath(),tempfn);

		// TODO: Make sure to limit recursion to avoid crashing.
		RawFile* newRawFile = new RawFile(fullPath);
		const wchar_t* psflibError = NULL;
		if (newRawFile->open(fullPath))
			psflibError = psf_read_exe(newRawFile, exebuffer, exebuffersize);
		else
			psflibError = L"Unable to open lib file.";
		delete fullPath;
		delete newRawFile;

		if (psflibError != NULL)
			return psflibError;

		libIndex++;
	}
	return NULL;
}
