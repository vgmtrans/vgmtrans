#include "stdafx.h"
#include "SPCLoader.h"
#include "Root.h"

SPCLoader::SPCLoader(void)
{
}

SPCLoader::~SPCLoader(void)
{
}

int SPCLoader::Apply(RawFile* file)
{
	if (file->size() >= 0x10100)
	{
		UINT sig = file->GetWord(0);
		if (sig == 0x53454E53)
		{
			sig = file->GetWord(4);
			UINT sig2 = file->GetWord(8);
			if (sig == 0x4350532D && (sig2 & 0xFFFFFF) == 0x303037)				//if the file begins "SNES-SPC700"
			{
				BYTE* spcData = new BYTE[0x10000];
				//::CopyMemory(spcData, m_pFileData+0x100, 0x10000);
				memcpy(spcData, file->buf.data+0x100, 0x10000);
				pRoot->CreateVirtFile(spcData, 0x10000, file->GetFileName());
				return DELETE_IT;
			}
		}
	}
	return KEEP_IT;
}