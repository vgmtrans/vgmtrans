#include "pch.h"
#include "PSFFile.h"
#include "SNSFLoader.h"
#include "Root.h"
#include <zlib.h>

using namespace std;

#define SNSF_VERSION	0x23
#define SNSF_MAX_ROM_SIZE	0x600000

wchar_t *GetFileWithBase(const wchar_t *f, const wchar_t *newfile);

SNSFLoader::SNSFLoader(void)
{
}

SNSFLoader::~SNSFLoader(void)
{
}

PostLoadCommand SNSFLoader::Apply(RawFile* file)
{
	uint8_t sig[4];
	file->GetBytes(0, 4, sig);
	if (memcmp(sig, "PSF", 3) == 0)
	{
		uint8_t version = sig[3];
		if (version == SNSF_VERSION)
		{
			const wchar_t *complaint;
			size_t exebufsize = SNSF_MAX_ROM_SIZE;
			uint8_t* exebuf = NULL;
			//memset(exebuf, 0, exebufsize);

			complaint = psf_read_exe(file, exebuf, exebufsize);
			if(complaint) 
			{
                Alert(complaint);
				delete[] exebuf;
				return KEEP_IT; 
			}
			//pRoot->UI_WriteBufferToFile(L"uncomp.smc", exebuf, exebufsize);

			wstring str = file->GetFileName();
			pRoot->CreateVirtFile(exebuf, (uint32_t)exebufsize, str.data(), L"", file->tag);
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
const wchar_t* SNSFLoader::psf_read_exe(
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

	DataSeg* snsfExeHeadSeg;
	if (!psf.ReadExeDataSeg(snsfExeHeadSeg, 0x08, 0))
		return psf.GetError();

	uint32_t snsfRomStart = snsfExeHeadSeg->GetWord(0x00);
	uint32_t snsfRomSize = snsfExeHeadSeg->GetWord(0x04);
	delete snsfExeHeadSeg;
	if (snsfRomStart + snsfRomSize > exebuffersize || (exebuffer == NULL && exebuffersize == 0))
		return L"SNSF ROM section start and/or size values are likely corrupt.";

	if (exebuffer == NULL)
	{
		exebuffersize = snsfRomStart + snsfRomSize;
		exebuffer = new uint8_t[exebuffersize];
		if (exebuffer == NULL)
		{
			return L"SNSF ROM memory allocation error.";
		}
		memset(exebuffer, 0, exebuffersize);
	}

	if (!psf.ReadExe(exebuffer + snsfRomStart, snsfRomSize, 0x08))
		return L"Decompression failed";

	// set tags to RawFile
	if (psf.tags.count("title") != 0) {
		file->tag.title = string2wstring(psf.tags["title"]);
	}
	if (psf.tags.count("artist") != 0) {
		file->tag.artist = string2wstring(psf.tags["artist"]);
	}
	if (psf.tags.count("game") != 0) {
		file->tag.album = string2wstring(psf.tags["game"]);
	}
	if (psf.tags.count("comment") != 0) {
		file->tag.comment = string2wstring(psf.tags["comment"]);
	}

	return NULL;
}

const wchar_t* SNSFLoader::load_psf_libs(PSFFile& psf, RawFile* file, unsigned char*& exebuffer, size_t& exebuffersize)
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

		wchar_t tempfn[PATH_MAX] = { 0 };
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
