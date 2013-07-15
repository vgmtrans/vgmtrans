#include "stdafx.h"
#include "RiffFile.h"
#include "common.h"

UINT Chunk::GetSize()
{
	return 8 + size;
}

void Chunk::SetData(const void* src, DWORD datasize)
{
	// Determine if we need to pad for word alignment
	bool bPad = datasize % 2;

	// set the size and copy from the data source
	size = datasize + bPad;
	if (data != 0)
		delete data;
	data = new BYTE[datasize];
	memcpy(data, src, datasize);

	// Add pad byte
	if (bPad)
		*(data+datasize) = 0;
}

void Chunk::Write(BYTE* buffer)
{
	memcpy(buffer, id, 4);
	*(DWORD*)(buffer+4) = size;
	memcpy(buffer+8, data, size);
}

Chunk* ListTypeChunk::AddChildChunk(Chunk* ck)
{
	childChunks.push_back(ck);
	return ck;
}

UINT ListTypeChunk::GetSize()
{
	UINT size = 12;		//id + size + "LIST"
	//for(Chunk ck : childChunks)			//C++0X syntax, not supported in MSVC (fuck Microsoft)
	//for_each (ck, childChunks)
	for	(auto iter = this->childChunks.begin(); iter != childChunks.end(); iter++)
		size += (*iter)->GetSize();
	return size;
}

void ListTypeChunk::Write(BYTE* buffer)
{
	memcpy(buffer, this->id, 4);
	*(DWORD*)(buffer+4) = GetSize()-8;
	memcpy(buffer+8, this->type, 4);
	UINT bufOffset = 12;
	//for(Chunk ck : childChunks)			//C++0X syntax, not supported in MSVC (fuck Microsoft)
	//for_each (ck, childChunks)
	for	(auto iter = this->childChunks.begin(); iter != childChunks.end(); iter++)
	{
		(*iter)->Write(buffer+bufOffset);
		bufOffset += (*iter)->GetSize();
	}
	//TODO: Add pad byte
}

RiffFile::RiffFile(string file_name, string form)
	: RIFFChunk(form),
	  name(file_name)
{
	//AlignName(name);
}
