#include "stdafx.h"
#include "PSF1Loader.h"
#include "Root.h"
#include <zlib.h>
#include ".\loaders\sexypsf\driver.h"

wchar_t *GetFileWithBase(const wchar_t *f, const wchar_t *newfile);

PSF1Loader::PSF1Loader(void)
{
}

PSF1Loader::~PSF1Loader(void)
{
}

int PSF1Loader::Apply(RawFile* file)
{
	UINT sig = file->GetWord(0);
	if ((sig & 0x00FFFFFF) == 0x465350 && ((sig & 0xFF000000) == 0x01000000 || (sig & 0xFF000000) == 0x11000000))
	{
		const wchar_t *complaint;
		UINT exeRealSize;
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

	zexebuf= new uint8[exe_size];
	if(!zexebuf)
		return L"Out of memory reading the file";
	file->GetBytes(16+reserved_size, exe_size, zexebuf);
	//fseek(f, 16+reserved_size, SEEK_SET);
	//fread(zexebuf, 1, exe_size, f);
	//fclose(f);

	if(exe_crc != crc32(crc32(0L, Z_NULL, 0), zexebuf, exe_size))
	{
		delete[] zexebuf;
		return L"CRC failure - executable data is corrupt";
	}

	// Now we get into the stuff related to recursive psflib loading.
	// the actual size of the header is 0x800, but we only need the first 0x20 for the text section offset/size
	BYTE exeHdr[0x20];
	destlen = sizeof(exeHdr);
	if (uncompress(exeHdr, &destlen, zexebuf, exe_size) == Z_DATA_ERROR)
	{
		delete[] zexebuf;
		return L"Decompression failed";
	}

	U32 textSectionStart = get32lsb(exeHdr+0x18) & 0x3FFFFF;
	U32 textSectionSize  = get32lsb(exeHdr+0x1C);
	if (textSectionStart + textSectionSize > 0x200000)
	{
		delete[] zexebuf;
		return L"Text section start and/or size values are corrupt in PSX-EXE header.";
	}

	// search exclusively for _lib tag, and if found, perform a recursive load
	// No, this isn't really proper, but i'm lazy at the moment.
	int tagSectionSize = file->GetSize() - (exe_size+0x10);
	char* tagSect = new char[tagSectionSize];
	file->GetBytes(exe_size+0x10, tagSectionSize, tagSect);
	for (int i = 0; i < tagSectionSize - 7; i++)	//5 bytes for sig, 1 for filename, 1 for 0x0A ending
	{
		if (tagSect[i] == '_' && tagSect[i+1] == 'l' &&
			tagSect[i+2] == 'i' && tagSect[i+3] == 'b' && tagSect[i+4] == '=')	//0x0A '_' 'l' 'i' 'b' '='
		{
			char* fnStart = tagSect+i+5;
			char* fnEnd = strchr(fnStart, 0x0A);
			

			wchar_t tempfn[_MAX_PATH];
			mbstowcs(tempfn, fnStart, fnEnd-fnStart);
			tempfn[fnEnd-fnStart] = 0;

			wchar_t *fullPath;
			fullPath=GetFileWithBase(file->GetFullPath(),tempfn);
			
			RawFile* newRawFile = new RawFile(fullPath);
			if (newRawFile->open(fullPath))
				psf_read_exe(newRawFile, exebuffer, exebuffersize);
			delete fullPath;
			delete newRawFile;
		}
	}
	delete[] tagSect;

	destlen = textSectionSize;//exebuffersize - textSectionStart - textSectionSize;
	if(uncompress(exebuffer+textSectionStart, &destlen, zexebuf, exe_size) == Z_DATA_ERROR)//!= Z_OK) 
	{
		delete[] zexebuf;
		return L"Decompression failed";
	}

	//realdestlen = destlen;
	//*realLength = destlen;

	/*
	** Okay, if the decompression worked, then the file is PROBABLY not corrupt.
	** Hooray for that.
	*/

	delete[] zexebuf;
	//free(zexebuf);

	return NULL;
}