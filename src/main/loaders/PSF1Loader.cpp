#include "pch.h"
#include "PSFFile.h"
#include "PSF1Loader.h"
#include "Root.h"

using namespace std;

char* GetFileWithBase(const char* f, const char* newfile);

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
      const char* complaint;
      const size_t exebufsize = 0x200000;
      uint8_t *exebuf = new uint8_t[exebufsize];
      memset(exebuf, 0, exebufsize);

      complaint = psf_read_exe(file, exebuf, exebufsize);
      if (complaint) {
        pRoot->AddLogItem(new LogItem(std::string(complaint), LOG_LEVEL_ERR, "PSF1Loader"));
        delete[] exebuf;
        return KEEP_IT;
      }

      string str = file->GetFileName();
      pRoot->CreateVirtFile(exebuf, exebufsize, str.data(), "", file->tag);
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
const char* PSF1Loader::psf_read_exe(
    RawFile *file,
    unsigned char *exebuffer,
    unsigned exebuffersize
) {
  uint32_t fileSize = file->size();
  if (fileSize >= 0x10000000)
    return "PSF too large - likely corrupt";

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
    return "Text section start and/or size values are corrupt in PSX-EXE header.";

  // search exclusively for _lib tag, and if found, perform a recursive load
  const char* psflibError = load_psf_libs(psf, file, exebuffer, exebuffersize);
  if (psflibError != NULL)
    return psflibError;

  if (!psf.ReadExe(exebuffer + textSectionStart, textSectionSize, 0x800))
    return "Decompression failed";

  // set tags to RawFile
  if (psf.tags.count("title") != 0) {
    file->tag.title = psf.tags["title"];
  }
  if (psf.tags.count("artist") != 0) {
    file->tag.artist = psf.tags["artist"];
  }
  if (psf.tags.count("game") != 0) {
    file->tag.album = psf.tags["game"];
  }
  if (psf.tags.count("comment") != 0) {
    file->tag.comment = psf.tags["comment"];
  }

  return NULL;
}

const char* PSF1Loader::load_psf_libs(PSFFile &psf,
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

    auto tempfn = itLibTag->second.c_str();
    auto fullPath = GetFileWithBase(file->GetFullPath().c_str(), tempfn);

    // TODO: Make sure to limit recursion to avoid crashing.
    RawFile *newRawFile = new RawFile(fullPath);
    const char* psflibError = NULL;
    if (newRawFile->open(fullPath))
      psflibError = psf_read_exe(newRawFile, exebuffer, exebuffersize);
    else
      psflibError = "Unable to open lib file.";
    delete fullPath;
    delete newRawFile;

    if (psflibError != NULL)
      return psflibError;

    libIndex++;
  }
  return NULL;
}
