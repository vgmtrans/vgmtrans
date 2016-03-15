#include "pch.h"
#include "PSF2Loader.h"
#include "Root.h"
#include <zlib.h>

PSF2Loader::PSF2Loader(void) {
}

PSF2Loader::~PSF2Loader(void) {
}

PostLoadCommand PSF2Loader::Apply(RawFile *file) {
  uint32_t sig = file->GetWord(0);
  if ((sig & 0x00FFFFFF) == 0x465350 && ((sig & 0xFF000000) == 0x02000000))    //if the sig is PSF 0x02
  {
    int r;
    int dircount;
    unsigned char hdr[16];

    unsigned long reserved_size;
    unsigned long exe_size;

    file->GetBytes(0, 0x10, hdr);

    reserved_size = get32lsb(hdr + 4);
    exe_size = get32lsb(hdr + 8);

    dircount = file->GetWord(0x10);

    r = psf2unpack(file, 0x14, dircount);
    return DELETE_IT;
  }
  return KEEP_IT;
}


uint32 PSF2Loader::get32lsb(uint8 *src) {
  return
      ((((uint32) (src[0])) & 0xFF) << 0) |
      ((((uint32) (src[1])) & 0xFF) << 8) |
      ((((uint32) (src[2])) & 0xFF) << 16) |
      ((((uint32) (src[3])) & 0xFF) << 24);
}

/////////////////////////////////////////////////////////////////////////////

int PSF2Loader::psf2_decompress_block(
    RawFile *file,
    unsigned fileoffset,
    unsigned blocknumber,
    unsigned numblocks,
    unsigned char *decompressedblock,
    unsigned blocksize
) {
  unsigned int i;
  unsigned long destlen;
  unsigned long current_block;
  uint8 *blocks;
  uint8 *zblock;
  blocks = new uint8[numblocks * 4];

  if (!blocks) {
    pRoot->AddLogItem(new LogItem(std::wstring(L"Out of Memory"), LOG_LEVEL_ERR, L"PSF2Loader"));
    return -1;
  }

  file->GetBytes(fileoffset, numblocks * 4, blocks);
  current_block = get32lsb(blocks + (blocknumber * 4));
  zblock = new uint8[current_block];

  if (!zblock) {
    pRoot->AddLogItem(new LogItem(std::wstring(L"Out of Memory"), LOG_LEVEL_ERR, L"PSF2Loader"));
    delete[] blocks;
    return -1;
  }

  int tempOffset = fileoffset + numblocks * 4;
  for (i = 0; i < blocknumber; i++)
    tempOffset += get32lsb(blocks + (i * 4));
  file->GetBytes(tempOffset, current_block, zblock);

  destlen = blocksize;
  if (uncompress(decompressedblock, &destlen, zblock, current_block) != Z_OK) {
    pRoot->AddLogItem(new LogItem(std::wstring(L"Decompression failed"), LOG_LEVEL_ERR, L"PSF2Loader"));
    delete[] zblock;
    delete[] blocks;
    return -1;
  }

  delete[] zblock;
  delete[] blocks;
  return 0;
}


int PSF2Loader::psf2unpack(RawFile *file, unsigned long fileoffset, unsigned long dircount) {
  unsigned int i, j, k;

  wchar_t wfilename[37];
  unsigned char filename[37];
  unsigned long offset = 0;
  unsigned long filesize = 0;
  unsigned long buffersize = 0;

  int r;

  unsigned int blockcount;
  uint8 *dblock;


  memset(filename, 0, sizeof(filename) / sizeof(filename[0]));

  for (i = 0; i < dircount; i++) {
    file->GetBytes(i * 48 + fileoffset, 36, filename);
    file->GetBytes(i * 48 + fileoffset + 36, 4, &offset);
    file->GetBytes(i * 48 + fileoffset + 36 + 4, 4, &filesize);
    file->GetBytes(i * 48 + fileoffset + 36 + 4 + 4, 4, &buffersize);
    if ((filesize == 0) && (buffersize == 0)) {
      file->GetBytes(offset + 0x10, 4, &filesize);

      r = psf2unpack(file, offset + 0x14, filesize);
      if (r) {
        pRoot->AddLogItem(new LogItem(std::wstring(L"Directory decompression failed"), LOG_LEVEL_ERR, L"PSF2Loader"));
        return -1;
      }
    }
    else {
      blockcount = ((filesize + buffersize) - 1) / buffersize;

      uint8_t *newdataBuf = new uint8_t[filesize];
      uint32_t actualFileSize = filesize;
      k = 0;

      dblock = new uint8[buffersize];
      if (!dblock) {
        pRoot->AddLogItem(new LogItem(std::wstring(L"Out of Memory"), LOG_LEVEL_ERR, L"PSF2Loader"));
        return -1;
      }

      for (j = 0; j < blockcount; j++) {
        r = psf2_decompress_block(file, offset + 0x10, j, blockcount, dblock, buffersize);

        if (r) {
          //string.Format("File %s failed to decompress",filename);
          return -1;
        }
        if (filesize > buffersize) {
          filesize -= buffersize;
          memcpy(newdataBuf + k, dblock, buffersize);
          k += buffersize;
        }
        else {
          memcpy(newdataBuf + k, dblock, filesize);
          k += filesize;
        }

      }

      mbstowcs(wfilename, (const char *) filename, sizeof(filename) / sizeof(filename[0]));
      pRoot->CreateVirtFile(newdataBuf, actualFileSize, wfilename, file->GetFullPath());
      delete[] dblock;
    }
  }
  return 0;
}