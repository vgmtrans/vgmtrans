#include "stdafx.h"
#include "PSFFile.h"
#include "PSF1Loader.h"
#include "Root.h"
#include <zlib.h>
#include ".\loaders\sexypsf\driver.h"

#define PSF_TAG_SIG             "[TAG]"
#define PSF_TAG_SIG_LEN         5

wchar_t *GetFileWithBase(const wchar_t *f, const wchar_t *newfile);

PSF1Loader::PSF1Loader(void)
{
}

PSF1Loader::~PSF1Loader(void)
{
}

PostLoadCommand PSF1Loader::Apply(RawFile* file)
{
	UINT sig = file->GetWord(0);
	if ((sig & 0x00FFFFFF) == 0x465350 && ((sig & 0xFF000000) == 0x01000000 || (sig & 0xFF000000) == 0x11000000))
	{
		const wchar_t *complaint;
		//UINT exeRealSize;
		BYTE* exebuf = new BYTE[0x200000];

		complaint = psf_read_exe(file, exebuf, 0x200000);
		if(complaint) 
		{ 
			Alert(complaint);
			delete[] exebuf;
			return KEEP_IT; 
		}
		//pRoot->UI_WriteBufferToFile(L"uncomp.raw", exebuf, 0x200000);

		//BYTE* cutbuf = new BYTE[exeRealSize];
		//memcpy(cutbuf, exebuf, exeRealSize);
		//delete[] exebuf;

		//DO EMULATION

		//if (!sexy_load(file->GetFullPath()))
		//{
		//	delete exebuf;
		//	return DELETE_IT;
		//}
		////sexy_execute(200000);//10000000);
		//sexy_execute(10000000);
		//
		//memcpy(exebuf, GetPSXMainMemBuf(), 0x200000);

		//pRoot->UI_WriteBufferToFile(L"dump.raw", exebuf, 0x200000);

		////EmulatePSX(cutbuf

		wstring str = file->GetFileName();
		//wstring::size_type pos = str.find_last_of('.');
		//str.erase(pos);
		//str.append(L" MemDump");

		//pRoot->CreateVirtFile(str.data(), exebuf, 0x200000);
		pRoot->CreateVirtFile(exebuf, 0x200000, str.data());
		return DELETE_IT;
	}

	


	return KEEP_IT;
}


uint32 PSF1Loader::get32lsb(uint8 *src) {
  return
    ((((uint32)(src[0])) & 0xFF) <<  0) |
    ((((uint32)(src[1])) & 0xFF) <<  8) |
    ((((uint32)(src[2])) & 0xFF) << 16) |
    ((((uint32)(src[3])) & 0xFF) << 24);
}


// The code below was written by Neill Corlett and adapted for VGMTrans.
// Recursive psflib loading has been added.


/***************************************************************************/
/*
** Read the EXE from a PSF file
**
** Returns the error message, or NULL on success
*/
const wchar_t* PSF1Loader::psf_read_exe(
  RawFile* file,
  unsigned char *exebuffer,
  unsigned exebuffersize
) 
{
	uint8 *zexebuf;
	uint8 *reservebuf;
	uLong destlen;
	uint32 reserved_size;
	uint32 exe_size;
	uint32 exe_crc;
	uint32 fl;
	uint8 hdr[0x10];

	//f=fopen(filename,"rb");
	//if(!f) return "Unable to open the file";
	//fseek(f,0,SEEK_END);fl=ftell(f);fseek(f,0,SEEK_SET);
	fl = file->size();
	/*
	** Idiot check for insanely large or small files
	*/
	if(fl >= 0x10000000)
		return L"PSF too large - likely corrupt";
	if(fl < 0x10)
		return L"PSF too small - likely corrupt";
	file->GetBytes(0, 0x10, hdr);
	if(memcmp(hdr, "PSF", 3))
		return L"Invalid PSF format";

	reserved_size = get32lsb(hdr +  4);
	exe_size      = get32lsb(hdr +  8);
	exe_crc       = get32lsb(hdr + 12);
	/*
	** Consistency check on section lengths
	*/
	if( (reserved_size > fl) ||
		(exe_size > fl) ||
		((16+reserved_size+exe_size) > fl))
		return L"PSF header is inconsistent";

	zexebuf= new uint8[exe_size ? exe_size : 1];
	if(!zexebuf)
	{
		return L"Out of memory reading the file";
	}
	file->GetBytes(16+reserved_size, exe_size, zexebuf);
	//fseek(f, 16+reserved_size, SEEK_SET);
	//fread(zexebuf, 1, exe_size, f);
	//fclose(f);

	if(exe_crc != crc32(crc32(0L, Z_NULL, 0), zexebuf, exe_size))
	{
		delete[] zexebuf;
		return L"CRC failure - executable data is corrupt";
	}

	reservebuf= new uint8[reserved_size ? reserved_size : 1];
	if(!reservebuf)
	{
		return L"Out of memory reading the file";
	}
	file->GetBytes(16, reserved_size, reservebuf);

	UINT tagSectionOffset = 16 + reserved_size + exe_size;
	UINT tagSectionSize = file->GetSize() - tagSectionOffset;
	bool hasTagSection = false;
	if (tagSectionSize >= PSF_TAG_SIG_LEN)
	{
		BYTE tagSig[5];
		file->GetBytes(tagSectionOffset, PSF_TAG_SIG_LEN, tagSig);
		if (memcmp(tagSig, PSF_TAG_SIG, PSF_TAG_SIG_LEN) == 0)
		{
			hasTagSection = true;
		}
	}

	// Check if tag field exists.
	char* tagSect = new char[tagSectionSize + 1];
	if(tagSect == NULL)
	{
		delete[] zexebuf;
		delete[] reservebuf;
		return L"Out of memory reading the file";
	}
	file->GetBytes(tagSectionOffset, tagSectionSize, tagSect);
	tagSect[tagSectionSize] = '\0';

	// Parse tag section. Details are available here:
	// http://wiki.neillcorlett.com/PSFTagFormat
	map<string, string> tagMap;
	UINT tagCurPos = PSF_TAG_SIG_LEN;
	while (tagCurPos < tagSectionSize)
	{
		// Search the end position of the current line.
		char* pNewLine = strchr(&tagSect[tagCurPos], 0x0a);
		if (pNewLine == NULL)
		{
			// Tag section must end with a newline.
			// Read the all remaining bytes if a newline lacks though.
			pNewLine = tagSect + tagSectionSize;
		}

		// Replace the newline with NUL,
		// for better C string function compatibility.
		*pNewLine = '\0';

		// Search the variable=value separator.
		char* pSeparator = strchr(&tagSect[tagCurPos], '=');
		if (pSeparator == NULL)
		{
			// Blank lines, or lines not of the form "variable=value", are ignored.
			tagCurPos = pNewLine + 1 - tagSect;
			continue;
		}

		// Determine the start/end position of variable.
		char* pVarName = &tagSect[tagCurPos];
		char* pVarNameEnd = pSeparator;
		char* pVarVal = pSeparator + 1;
		char* pVarValEnd = pNewLine;

		// Whitespace at the beginning/end of the line and before/after the = are ignored.
		// All characters 0x01-0x20 are considered whitespace.
		// (There must be no null (0x00) characters.)
		// Trim them.
		while (pVarNameEnd > pVarName && *(unsigned char*)(pVarNameEnd - 1) <= 0x20)
			pVarNameEnd--;
		while (pVarValEnd > pVarVal && *(unsigned char*)(pVarValEnd - 1) <= 0x20)
			pVarValEnd--;
		while (pVarName < pVarNameEnd && *(unsigned char*)pVarName <= 0x20)
			pVarName++;
		while (pVarVal < pVarValEnd && *(unsigned char*)pVarVal <= 0x20)
			pVarVal++;

		// Read variable=value as string.
		string varName(pVarName, pVarNameEnd - pVarName);
		string varVal(pVarVal, pVarValEnd - pVarVal);

		// Multiple-line variables must appear as consecutive lines using the same variable name.
		// For instance:
		//   comment=This is a
		//   comment=multiple-line
		//   comment=comment.
		// Therefore, check if the variable had already appeared.
		map<string, string>::iterator it = tagMap.lower_bound(varName);
		if (it != tagMap.end() && it->first == varName)
		{
			it->second += "\n";
			it->second += varVal;
		}
		else
		{
			tagMap.insert(it, make_pair(varName, varVal));
		}

		tagCurPos = pNewLine + 1 - tagSect;
	}
	delete[] tagSect;

	// Now we get into the stuff related to recursive psflib loading.
	// the actual size of the header is 0x800, but we only need the first 0x20 for the text section offset/size
	BYTE exeHdr[0x20];
	destlen = sizeof(exeHdr);
	if (uncompress(exeHdr, &destlen, zexebuf, exe_size) == Z_DATA_ERROR)
	{
		delete[] zexebuf;
		delete[] reservebuf;
		return L"Decompression failed";
	}

	U32 textSectionStart = get32lsb(exeHdr+0x18) & 0x3FFFFF;
	U32 textSectionSize  = get32lsb(exeHdr+0x1C);
	if (textSectionStart + textSectionSize > 0x200000)
	{
		delete[] zexebuf;
		delete[] reservebuf;
		return L"Text section start and/or size values are corrupt in PSX-EXE header.";
	}

	// search exclusively for _lib tag, and if found, perform a recursive load
	// TODO: process _libN (N=2 and up)
	map<string, string>::iterator itLibTag = tagMap.find("_lib");
	if (itLibTag != tagMap.end())
	{
		wchar_t tempfn[_MAX_PATH] = { 0 };
		mbstowcs(tempfn, itLibTag->second.c_str(), itLibTag->second.size());

		wchar_t *fullPath;
		fullPath=GetFileWithBase(file->GetFullPath(),tempfn);

		// TODO: Make sure to limit recursion to avoid crashing.
		RawFile* newRawFile = new RawFile(fullPath);
		if (newRawFile->open(fullPath))
			psf_read_exe(newRawFile, exebuffer, exebuffersize);
		delete fullPath;
		delete newRawFile;
	}

	destlen = textSectionSize;//exebuffersize - textSectionStart - textSectionSize;
	if(uncompress(exebuffer+textSectionStart, &destlen, zexebuf, exe_size) == Z_DATA_ERROR)//!= Z_OK) 
	{
		delete[] zexebuf;
		delete[] reservebuf;
		return L"Decompression failed";
	}

	//realdestlen = destlen;
	//*realLength = destlen;

	/*
	** Okay, if the decompression worked, then the file is PROBABLY not corrupt.
	** Hooray for that.
	*/

	delete[] zexebuf;
	delete[] reservebuf;
	//free(zexebuf);

	return NULL;
}