#include "stdafx.h"
#include "RiffFile.h"
#include "common.h"

UINT Chunk::GetSize()
{
	return 8 + GetPaddedSize(size);
}

void Chunk::SetData(const void* src, DWORD datasize)
{
	size = datasize;

	// set the size and copy from the data source
	datasize = GetPaddedSize(size);
	if (data != NULL) {
		delete[] data;
		data = NULL;
	}
	data = new BYTE[datasize];
	memcpy(data, src, size);

	// Add pad byte
	DWORD padsize = datasize - size;
	if (padsize != 0) {
		memset(data + size, 0, padsize);
	}
}

void Chunk::Write(BYTE* buffer)
{
	memcpy(buffer, id, 4);
	*(DWORD*)(buffer+4) = size;
	memcpy(buffer+8, data, GetPaddedSize(size));
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
	return GetPaddedSize(size);
}

void ListTypeChunk::Write(BYTE* buffer)
{
	memcpy(buffer, this->id, 4);
	memcpy(buffer+8, this->type, 4);

	UINT bufOffset = 12;
	//for(Chunk ck : childChunks)			//C++0X syntax, not supported in MSVC (fuck Microsoft)
	//for_each (ck, childChunks)
	for	(auto iter = this->childChunks.begin(); iter != childChunks.end(); iter++)
	{
		(*iter)->Write(buffer+bufOffset);
		bufOffset += (*iter)->GetSize();
	}

	DWORD size = bufOffset;
	*(DWORD*)(buffer+4) = size - 8;

	// Add pad byte
	DWORD padsize = GetPaddedSize(size) - size;
	if (padsize != 0)
	{
		memset(data + size, 0, padsize);
	}
}

RiffFile::RiffFile(string file_name, string form)
	: RIFFChunk(form),
	  name(file_name)
{
	//AlignName(name);
}
