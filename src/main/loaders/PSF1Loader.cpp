/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #include "pch.h"
#include "PSFFile.h"
#include "PSF1Loader.h"
#include "Root.h"

using namespace std;

wchar_t *GetFileWithBase(const wchar_t *f, const wchar_t *newfile);

PSF1Loader::PSF1Loader(void) {
}

PSF1Loader::~PSF1Loader(void) {
}

PostLoadCommand PSF1Loader::Apply(RawFile *file) {
  uint8_t sig[4];
  file->GetBytes(0, 4, sig);
  if (memcmp(sig, "PSF", 3) == 0) {
    uint8_t version = sig[3];
    if (version == 0x01) {
      const wchar_t *complaint;
      const size_t exebufsize = 0x200000;
      //uint32_t exeRealSize;
      uint8_t *exebuf = new uint8_t[exebufsize];
      memset(exebuf, 0, exebufsize);

      complaint = psf_read_exe(file, exebuf, exebufsize);
      if (complaint) {
        pRoot->AddLogItem(new LogItem(std::wstring(complaint), LOG_LEVEL_ERR, L"PSF1Loader"));
        delete[] exebuf;
        return KEEP_IT;
      }
      //pRoot->UI_WriteBufferToFile(L"uncomp.raw", exebuf, 0x200000);

      //uint8_t* cutbuf = new uint8_t[exeRealSize];
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
      pRoot->CreateVirtFile(exebuf, exebufsize, str.data(), L"", file->tag);
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
const wchar_t *PSF1Loader::psf_read_exe(
    RawFile *file,
    unsigned char *exebuffer,
    unsigned exebuffersize
) {
  uint32_t fileSize = file->size();
  if (fileSize >= 0x10000000)
    return L"PSF too large - likely corrupt";

  PSFFile psf;
  if (!psf.Load(file))
    return psf.GetError();

  // Now we get into the stuff related to recursive psflib loading.
  // the actual size of the header is 0x800, but we only need the first 0x20 for the text section offset/size
  DataSeg *psfExeHeadSeg;
  if (!psf.ReadExeDataSeg(psfExeHeadSeg, 0x20, 0))
    return psf.GetError();

  uint32_t textSectionStart = psfExeHeadSeg->GetWord(0x18) & 0x3FFFFF;
  uint32_t textSectionSize = psfExeHeadSeg->GetWord(0x1C);
  delete psfExeHeadSeg;
  if (textSectionStart + textSectionSize > 0x200000)
    return L"Text section start and/or size values are corrupt in PSX-EXE header.";

  // search exclusively for _lib tag, and if found, perform a recursive load
  const wchar_t *psflibError = load_psf_libs(psf, file, exebuffer, exebuffersize);
  if (psflibError != NULL)
    return psflibError;

  if (!psf.ReadExe(exebuffer + textSectionStart, textSectionSize, 0x800))
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

const wchar_t *PSF1Loader::load_psf_libs(PSFFile &psf,
                                         RawFile *file,
                                         unsigned char *exebuffer,
                                         unsigned exebuffersize) {
  char libTagName[16];
  int libIndex = 1;
  while (true) {
    if (libIndex == 1)
      strcpy(libTagName, "_lib");
    else
      sprintf(libTagName, "_lib%d", libIndex);

    map<string, string>::iterator itLibTag = psf.tags.find(libTagName);
    if (itLibTag == psf.tags.end())
      break;

    wchar_t tempfn[PATH_MAX] = {0};
    mbstowcs(tempfn, itLibTag->second.c_str(), itLibTag->second.size());

    wchar_t *fullPath;
    fullPath = GetFileWithBase(file->GetFullPath(), tempfn);

    // TODO: Make sure to limit recursion to avoid crashing.
    RawFile *newRawFile = new RawFile(fullPath);
    const wchar_t *psflibError = NULL;
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
