#pragma once
#include "Loader.h"


#define uint32 unsigned
#define uint16 unsigned short
#define uint8 unsigned char

class PSF1Loader :
	public VGMLoader
{
public:
	PSF1Loader(void);
public:
	virtual ~PSF1Loader(void);

	virtual int Apply(RawFile* theFile);
	const wchar_t* psf_read_exe(RawFile* file, unsigned char *exebuffer, unsigned exebuffersize);

	uint32 get32lsb(uint8 *src);
};



