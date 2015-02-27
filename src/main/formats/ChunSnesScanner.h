#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum ChunSnesVersion : uint8_t; // see ChunSnesFormat.h

class ChunSnesScanner :
	public VGMScanner
{
public:
	ChunSnesScanner(void)
	{
		USE_EXTENSION(L"spc");
	}
	virtual ~ChunSnesScanner(void)
	{
	}

	virtual void Scan(RawFile* file, void* info = 0);
	void SearchForChunSnesFromARAM(RawFile* file);
	void SearchForChunSnesFromROM(RawFile* file);

private:
	static BytePattern ptnLoadSeqSummerV2;
	static BytePattern ptnLoadSeqWinterV1V2;
	static BytePattern ptnLoadSeqWinterV3;
	static BytePattern ptnSaveSongIndexSummerV2;
	static BytePattern ptnDSPInitTable;
	static BytePattern ptnProgChangeVCmdSummer;
	static BytePattern ptnProgChangeVCmdWinter;
};
