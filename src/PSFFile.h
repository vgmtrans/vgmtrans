#pragma once

#include <zlib.h>
#include "types.h"
#include "DataSeg.h"
#include "RawFile.h"

class PSFFile
{
public:
	PSFFile(void);
	PSFFile(RawFile* file);
	virtual ~PSFFile(void);

	bool Load(RawFile* file);
	bool ReadExe(BYTE* buf, size_t len, size_t stripLen) const;
	bool ReadExeDataSeg(DataSeg*& seg, size_t len, size_t stripLen) const;
	bool Decompress(size_t decompressed_size);
	bool IsDecompressed(void) const;
	BYTE GetVersion(void) const;
	size_t GetExeSize(void) const;
	size_t GetCompressedExeSize(void) const;
	size_t GetReservedSize(void) const;
	void Clear(void);
	const wchar_t* GetError(void) const;

public:
	PSFFile* parent;
	DataSeg& exe(void) { return *exeData; }
	DataSeg& reserved(void) { return *reservedData; }
	std::map<string, string> tags;

private:
	BYTE version;
	DataSeg* exeData; // decompressed program section, valid only when it has been decompressed
	DataSeg* exeCompData;
	DataSeg* reservedData;
	uint32_t exeCRC;
	bool decompressed;
	wchar_t* errorstr;
	BYTE* stripBuf;
	size_t stripBufSize;

	int myuncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, uLong stripLen) const;
};
