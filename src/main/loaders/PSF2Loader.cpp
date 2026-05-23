/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PSF2Loader.h"
#include <zlib.h>
#include "LogManager.h"
#include "components/PSFFile.h"

namespace vgmtrans::loaders {
LoaderRegistration<PSF2Loader> _psf2("PSF2");
}

void PSF2Loader::apply(const RawFile *file) {
    /* Don't bother on a file too small */
    if (file->size() < 0x10) {
      return;
}

    uint32_t sig = file->readWord(0);
    if ((sig & 0x00FFFFFF) == 0x465350 && ((sig & 0xFF000000) == 0x02000000)) {
        auto dircount = file->get<uint32_t>(0x10);
        psf2unpack(file, 0x14, dircount);
  }
}

static uint32_t get32lsb(const uint8_t *src) {
    return (((static_cast<uint32_t>(src[0])) & 0xFF) << 0) | (((static_cast<uint32_t>(src[1])) & 0xFF) << 8) |
           (((static_cast<uint32_t>(src[2])) & 0xFF) << 16) | (((static_cast<uint32_t>(src[3])) & 0xFF) << 24);
}

int PSF2Loader::psf2_decompress_block(const RawFile *file, unsigned fileoffset,
                                      unsigned blocknumber, unsigned numblocks,
                                      unsigned char *decompressedblock, unsigned blocksize) {
  unsigned long destlen;
  uint8_t *blocks = new uint8_t[numblocks * 4];

  file->readBytes(fileoffset, numblocks * 4, blocks);
  unsigned long current_block = get32lsb(blocks + (blocknumber * 4));
  uint8_t *zblock = new uint8_t[current_block];

  int tempOffset = fileoffset + numblocks * 4;
  for (uint32_t i = 0; i < blocknumber; i++)
    tempOffset += get32lsb(blocks + (i * 4));
  file->readBytes(tempOffset, current_block, zblock);

  destlen = blocksize;
  if (uncompress(decompressedblock, &destlen, zblock, current_block) != Z_OK) {
    L_ERROR("Decompression failed");
    delete[] zblock;
    delete[] blocks;
    return -1;
  }

  delete[] zblock;
  delete[] blocks;
  return 0;
}

int PSF2Loader::psf2unpack(const RawFile *file, unsigned long fileoffset, unsigned long dircount) {
  char filename[37];
  unsigned long offset = 0;
  unsigned long filesize = 0;
  unsigned long buffersize = 0;

  int r;

  memset(filename, 0, std::size(filename));

  for (uint32_t i = 0; i < dircount; i++) {
    file->readBytes(i * 48 + fileoffset, 36, filename);
    file->readBytes(i * 48 + fileoffset + 36, 4, &offset);
    file->readBytes(i * 48 + fileoffset + 36 + 4, 4, &filesize);
    file->readBytes(i * 48 + fileoffset + 36 + 4 + 4, 4, &buffersize);
    if ((filesize == 0) && (buffersize == 0)) {
      file->readBytes(offset + 0x10, 4, &filesize);

      r = psf2unpack(file, offset + 0x14, filesize);
      if (r) {
        L_ERROR("Directory decompression failed");
        return -1;
      }
    } else {
      uint32_t blockcount = ((filesize + buffersize) - 1) / buffersize;

      uint8_t *newdataBuf = new uint8_t[filesize];
      uint32_t actualFileSize = filesize;
      uint32_t k = 0;

      std::ptrdiff_t test = 4;
      uint16_t test2 = test;

      uint8_t *dblock = new uint8_t[buffersize];

      for (uint32_t j = 0; j < blockcount; j++) {
        r = psf2_decompress_block(file, offset + 0x10, j, blockcount, dblock, buffersize);

        if (r) {
          //string.Format("File %s failed to decompress",filename);
          delete[] dblock;
          delete[] newdataBuf;
          return -1;
        }
        if (filesize > buffersize) {
          filesize -= buffersize;
          memcpy(newdataBuf + k, dblock, buffersize);
          k += buffersize;
        } else {
          memcpy(newdataBuf + k, dblock, filesize);
          k += filesize;
        }
      }

      enqueue(new VirtFile(newdataBuf, actualFileSize, filename, file->path()));
      delete[] dblock;
      delete[] newdataBuf;
    }
  }

  return 0;
}
