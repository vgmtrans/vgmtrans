/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PSF2Loader.h"

#include "base/Types.h"
#include "components/PSFFile.h"
#include "LogManager.h"

#include <memory>
#include <vector>

#include <zlib.h>

namespace vgmtrans::loaders {
LoaderRegistration<PSF2Loader> _psf2("PSF2");
}

void PSF2Loader::apply(const RawFile *file) {
    /* Don't bother on a file too small */
    if (file->size() < 0x10) {
      return;
}

    u32 sig = file->readWord(0);
    if ((sig & 0x00FFFFFF) == 0x465350 && ((sig & 0xFF000000) == 0x02000000)) {
        auto dircount = file->get<u32>(0x10);
        psf2unpack(file, 0x14, dircount);
  }
}

static u32 get32lsb(const u8 *src) {
    return (((static_cast<u32>(src[0])) & 0xFF) << 0) | (((static_cast<u32>(src[1])) & 0xFF) << 8) |
           (((static_cast<u32>(src[2])) & 0xFF) << 16) | (((static_cast<u32>(src[3])) & 0xFF) << 24);
}

int PSF2Loader::psf2_decompress_block(const RawFile *file, unsigned fileoffset,
                                      unsigned blocknumber, unsigned numblocks,
                                      unsigned char *decompressedblock, unsigned blocksize) {
  unsigned long destlen;
  std::vector<u8> blocks(numblocks * 4);

  file->readBytes(fileoffset, numblocks * 4, blocks.data());
  unsigned long current_block = get32lsb(blocks.data() + (blocknumber * 4));
  std::vector<u8> zblock(current_block);

  int tempOffset = fileoffset + numblocks * 4;
  for (u32 i = 0; i < blocknumber; i++)
    tempOffset += get32lsb(blocks.data() + (i * 4));
  file->readBytes(tempOffset, current_block, zblock.data());

  destlen = blocksize;
  if (uncompress(decompressedblock, &destlen, zblock.data(), current_block) != Z_OK) {
    L_ERROR("Decompression failed");
    return -1;
  }

  return 0;
}

int PSF2Loader::psf2unpack(const RawFile *file, unsigned long fileoffset, unsigned long dircount) {
  char filename[37];
  unsigned long offset = 0;
  unsigned long filesize = 0;
  unsigned long buffersize = 0;

  int r;

  memset(filename, 0, std::size(filename));

  for (u32 i = 0; i < dircount; i++) {
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
      u32 blockcount = ((filesize + buffersize) - 1) / buffersize;

      u32 actualFileSize = filesize;
      std::vector<u8> newdataBuf(actualFileSize);
      u32 k = 0;

      std::vector<u8> dblock(buffersize);

      for (u32 j = 0; j < blockcount; j++) {
        r = psf2_decompress_block(file, offset + 0x10, j, blockcount, dblock.data(), buffersize);

        if (r) {
          //string.Format("File %s failed to decompress",filename);
          return -1;
        }
        if (filesize > buffersize) {
          filesize -= buffersize;
          memcpy(newdataBuf.data() + k, dblock.data(), buffersize);
          k += buffersize;
        } else {
          memcpy(newdataBuf.data() + k, dblock.data(), filesize);
          k += filesize;
        }
      }

      enqueue(std::make_unique<VirtFile>(newdataBuf.data(), actualFileSize, filename, file->path()));
    }
  }

  return 0;
}
