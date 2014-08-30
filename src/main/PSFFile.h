#pragma once

#include <zlib.h>
#include <stdint.h>
#include "DataSeg.h"
#include "RawFile.h"

class PSFFile
{
public:
	PSFFile(void);
	PSFFile(RawFile* file);
	virtual ~PSFFile(void);

	bool Load(RawFile* file);
	bool ReadExe(uint8_t* buf, size_t len, size_t stripLen) const;
	bool ReadExeDataSeg(DataSeg*& seg, size_t len, size_t stripLen) const;
	bool Decompress(size_t decompressed_size);
	bool IsDecompressed(void) const;
	uint8_t GetVersion(void) const;
	size_t GetExeSize(void) const;
	size_t GetCompressedExeSize(void) const;
	size_t GetReservedSize(void) const;
	void Clear(void);
	const wchar_t* GetError(void) const;

public:
	PSFFile* parent;
	DataSeg& exe(void) { return *exeData; }
	DataSeg& reserved(void) { return *reservedData; }
	std::map<std::string, std::string> tags;

private:
	uint8_t version;
	DataSeg* exeData; // decompressed program section, valid only when it has been decompressed
	DataSeg* exeCompData;
	DataSeg* reservedData;
	uint32_t exeCRC;
	bool decompressed;
	wchar_t* errorstr;
	uint8_t* stripBuf;
	size_t stripBufSize;

	int myuncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, uLong stripLen) const;
};
