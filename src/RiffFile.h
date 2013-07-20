#pragma once

#include "common.h"


//////////////////////////////////////////////
// Chunk		- Riff format chunk
//////////////////////////////////////////////
class Chunk {
public:
	char id[4];		//  A chunk ID identifies the type of data within the chunk.
	DWORD size;		//  The size of the chunk data in bytes, excluding any pad byte.
	BYTE* data;		//  The actual data not including a possible pad byte to word align

public:
	Chunk(string theId)
		: data(NULL),
		  size(0)
	{
		assert(theId.length() == 4);
		memcpy(id, theId.c_str(), 4); 
	}
	virtual ~Chunk()
	{
		if (data != NULL) {
			delete[] data;
			data = NULL;
		}
	}
	void SetData(const void* src, DWORD datasize);
	virtual UINT GetSize();	//  Returns the size of the chunk in bytes, including any pad byte.
	virtual void Write(BYTE* buffer);

protected:
	static inline DWORD GetPaddedSize(DWORD size) {
		//DWORD n = size % align;
		//return size + (n == 0 ? 0 : (align - n));
		return size + (size % 2);
	}
};


////////////////////////////////////////////////////////////////////////////
// ListTypeChunk	- Riff chunk type where the first 4 data bytes are a sig
//					  and the rest of the data is a collection of child chunks
////////////////////////////////////////////////////////////////////////////
class ListTypeChunk : public Chunk
{
public:
	char type[4];	// 4 byte sig that begins the data field, "LIST" or "sfbk" for ex
	std::list<Chunk*> childChunks;

public:
	ListTypeChunk(string theId, string theType)
		: Chunk(theId)
	{ 
		assert(theType.length() == 4);
		memcpy(type, theType.c_str(), 4); 
	}
	virtual ~ListTypeChunk()
	{
		DeleteList(childChunks);
	}

	Chunk* AddChildChunk(Chunk* ck);
	virtual UINT GetSize();	//  Returns the size of the chunk in bytes, including any pad byte.
	virtual void Write(BYTE* buffer);
};

////////////////////////////////////////////////////////////////////////////
// RIFFChunk
////////////////////////////////////////////////////////////////////////////
class RIFFChunk : public ListTypeChunk
{
public:
	RIFFChunk(string form) : ListTypeChunk("RIFF", form) {}
};

////////////////////////////////////////////////////////////////////////////
// LISTChunk
////////////////////////////////////////////////////////////////////////////
class LISTChunk : public ListTypeChunk
{
public:
	LISTChunk(string type) : ListTypeChunk("LIST", type) {}
};


////////////////////////////////////////////////////////////////////////////
// RiffFile		- 
////////////////////////////////////////////////////////////////////////////
class RiffFile : public RIFFChunk
{
public:
	RiffFile(string file_name, string form);

	static void RiffFile::WriteLIST(vector<BYTE> & buf, UINT listName, UINT listSize)
	{
		PushTypeOnVectBE<UINT>(buf, 0x4C495354);	//write "LIST"
		PushTypeOnVect<UINT>(buf, listSize);
		PushTypeOnVectBE<UINT>(buf, listName);
	}

	//Adds a null byte and ensures 16 bit alignment of a text string
	static void RiffFile::AlignName(string &name)
	{
		name += (char)0x00;
		if (name.size() % 2)						//if the size of the name string is odd
			name += (char)0x00;						//add another null byte
	}


protected:
	string name;
};