#pragma once
#include "Loader.h"

#define uint32 unsigned
#define uint16 unsigned short
#define uint8 unsigned char

class PSF2Loader :
	public VGMLoader
{
public:
	PSF2Loader(void);
public:
	virtual ~PSF2Loader(void);

	virtual int Apply(RawFile* theFile);
	uint32 get32lsb(uint8 *src);
	int psf2_decompress_block(RawFile* file,
							  unsigned fileoffset,
							  unsigned blocknumber,
							  unsigned numblocks,
							  unsigned char *decompressedblock,
							  unsigned blocksize);
	int psf2unpack(RawFile* file, unsigned long fileoffset, unsigned long dircount);
};
