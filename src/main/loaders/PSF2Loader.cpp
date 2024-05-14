/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PSF2Loader.h"
#include <zlib-ng.h>
#include "Root.h"
#include "LogManager.h"
#include "components/PSFFile.h"

void PSF2Loader::apply(const RawFile *file) {
    /* Don't bother on a file too small */
    if (file->size() < 0x10) {
      return;
}

    u32 sig = file->GetWord(0);
    if ((sig & 0x00FFFFFF) == 0x465350 && ((sig & 0xFF000000) == 0x02000000)) {
        auto dircount = file->get<u32>(0x10);
        psf2unpack(file, 0x14, dircount);
  }
}

static u32 get32lsb(u8 *src) {
    return ((((u32)(src[0])) & 0xFF) << 0) | ((((u32)(src[1])) & 0xFF) << 8) |
           ((((u32)(src[2])) & 0xFF) << 16) | ((((u32)(src[3])) & 0xFF) << 24);
}

int PSF2Loader::psf2_decompress_block(const RawFile *file, unsigned fileoffset,
                                      unsigned blocknumber, unsigned numblocks,
                                      unsigned char *decompressedblock, unsigned blocksize) {
  unsigned int i;
  size_t destlen;
  unsigned long current_block;
    u8 *blocks;
    u8 *zblock;
    blocks = new u8[numblocks * 4];

  if (!blocks) {
    L_ERROR("Out of memory");
    return -1;
  }

  file->GetBytes(fileoffset, numblocks * 4, blocks);
  current_block = get32lsb(blocks + (blocknumber * 4));
    zblock = new u8[current_block];

  if (!zblock) {
    L_ERROR("Out of memory");
    delete[] blocks;
    return -1;
  }

  int tempOffset = fileoffset + numblocks * 4;
  for (i = 0; i < blocknumber; i++)
    tempOffset += get32lsb(blocks + (i * 4));
  file->GetBytes(tempOffset, current_block, zblock);

  destlen = blocksize;
  if (zng_uncompress(decompressedblock, &destlen, zblock, current_block) != Z_OK) {
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
  unsigned int i, j, k;

  char filename[37];
  unsigned long offset = 0;
  unsigned long filesize = 0;
  unsigned long buffersize = 0;

  int r;

  unsigned int blockcount;
  u8 *dblock;

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
        L_ERROR("Directory decompression failed");
        return -1;
      }
    } else {
      blockcount = ((filesize + buffersize) - 1) / buffersize;

      u8 *newdataBuf = new u8[filesize];
      u32 actualFileSize = filesize;
      k = 0;

      dblock = new u8[buffersize];
      if (!dblock) {
        L_ERROR("Out of memory");
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
        } else {
          memcpy(newdataBuf + k, dblock, filesize);
          k += filesize;
        }
      }

      enqueue(new VirtFile(newdataBuf, actualFileSize, filename, file->path()));
      delete[] dblock;
    }
  }

  return 0;
}
