#pragma once

#include <deque>

using namespace std;

//DataSeg is a very simple class for allocating a block of data with a
//variable reference point for indexing.  Some formats use
//absolute offset references (the Nintendo SNES seq format, for example)

class DataSeg
{
public:
	DataSeg(void);
	~DataSeg(void);

	inline BYTE& operator[](ULONG offset)
	{
		assert(offset >= startOff && (offset < (startOff+size)));
		//if(offset >= startOff && (offset < (startOff+size)))
			return data[offset-startOff];
		//else
		//	assert(AttemptExpand(offset));
	}

	//bool AttemptExpand(ULONG offset);
	//void insert_front(BYTE* buf, ULONG size);
	//void insert(ULONG index, BYTE* buf, ULONG size);
	//void insert_back(BYTE* buf, ULONG size);
	//void copy(
	//void resize(ULONG newStartOff, ULONG newSize);
public:
	void reposition(ULONG newBegin);
	void load(BYTE* buf, ULONG startVirtAddr, ULONG theSize);
	void alloc(ULONG theSize);
	void clear();


	inline void GetBytes(UINT nIndex, UINT nCount, void* pBuffer)
	{
		assert((nIndex >= startOff) && (nIndex+nCount <= endOff));
		memcpy(pBuffer, data+nIndex-startOff, nCount);
	}

	inline BYTE GetByte(UINT nIndex)
	{
		assert((nIndex >= startOff) && (nIndex+1 <= endOff));
		return data[nIndex-startOff];
	}

	inline USHORT GetShort(UINT nIndex)
	{
		assert((nIndex >= startOff) && (nIndex+2 <= endOff));
		return *((USHORT*)(data+nIndex-startOff));
	}

	inline UINT GetWord(UINT nIndex)
	{
		assert((nIndex >= startOff) && (nIndex+4 <= endOff));
		return *((UINT*)(data+nIndex-startOff));
	}

	inline USHORT GetShortBE(UINT nIndex)
	{
		assert((nIndex >= startOff) && (nIndex+2 <= endOff));
		return ((BYTE)(data[nIndex-startOff]) << 8) + ((BYTE)data[nIndex+1-startOff]);
	}

	inline UINT GetWordBE(UINT nIndex)
	{
		assert((nIndex >= startOff) && (nIndex+4 <= endOff));
		return ((BYTE)data[nIndex-startOff] << 24) + ((BYTE)data[nIndex+1-startOff] << 16)
			 + ((BYTE)data[nIndex+2-startOff] << 8) + (BYTE)data[nIndex+3-startOff];
	}

	inline bool IsValidOffset(UINT nIndex)
	{
		return (nIndex >= startOff && nIndex < endOff);
	}


public:
	//UCHAR *data;
	//deque<BYTE> data;
	BYTE* data;

	ULONG startOff;
	ULONG endOff;
	ULONG size;
	bool bAlloced;
};


class CompDataSeg : public DataSeg
{
public:
	CompDataSeg(void);
	~CompDataSeg(void);

	void load(BYTE* buf, ULONG startVirtAddr, ULONG theSize, ULONG compBufSize);
	void clear();

	
	void UpdateCompBuf(ULONG offset);

	inline BYTE& operator[](ULONG offset)
	{
		if (offset < compBlockVirtBegin || offset >= compBlockVirtEnd)
			UpdateCompBuf(offset);
		return data[offset-compBlockVirtBegin];
	}

	inline void GetBytes(UINT nIndex, UINT nCount, void* pBuffer)
	{
		if ((nIndex + nCount) > endOff)
			nCount = endOff - nIndex;
		if (nCount < 0)
			nCount = 0;
		//int nSegments = (nIndex+nCount-compBlockVirtBegin) / compBufSize;
		//if ((nIndex+nCount-compBlockVirtBegin) % compBufSize)
		//	nSegments++;
		ULONG curOffset = 0;
		while (nCount)
		{
			if (nIndex < compBlockVirtBegin || nIndex >= compBlockVirtEnd)
				UpdateCompBuf(nIndex);
			ULONG transferSize;
			if (nCount > compBlockSize)
				transferSize = compBlockSize;
			else
				transferSize = nCount;
			if (nIndex + transferSize > compBlockVirtEnd)
				transferSize = compBlockVirtEnd-nIndex;
			memcpy((BYTE*)pBuffer+curOffset, (BYTE*)data+nIndex-compBlockVirtBegin, transferSize);
			nCount -= transferSize;
			nIndex += transferSize;
			curOffset += transferSize;
		}
	}

	inline BYTE GetByte(UINT nIndex)
	{
		return data[nIndex-startOff];
	}

	inline USHORT GetShort(UINT nIndex)
	{
		//assert((nIndex >= startOff) && (nIndex <= endOff));
		if (nIndex < compBlockVirtBegin || nIndex >= compBlockVirtEnd)
			UpdateCompBuf(nIndex);
		return *((USHORT*)(data+nIndex-compBlockVirtBegin));
	}

	inline UINT GetWord(UINT nIndex)
	{
		if (nIndex < compBlockVirtBegin || nIndex >= compBlockVirtEnd)
			UpdateCompBuf(nIndex);
		return *((UINT*)(data+nIndex-compBlockVirtBegin));
	}

	inline USHORT GetShortBE(UINT nIndex)
	{
		if (nIndex < compBlockVirtBegin || nIndex >= compBlockVirtEnd)
			UpdateCompBuf(nIndex);
		return ((BYTE)(data[nIndex-compBlockVirtBegin]) << 8) + ((BYTE)data[nIndex+1-compBlockVirtBegin]);
	}

	inline UINT GetWordBE(UINT nIndex)
	{
		if (nIndex < compBlockVirtBegin || nIndex >= compBlockVirtEnd)
			UpdateCompBuf(nIndex);
		return ((BYTE)data[nIndex-compBlockVirtBegin] << 24) + ((BYTE)data[nIndex+1-compBlockVirtBegin] << 16)
			 + ((BYTE)data[nIndex+2-compBlockVirtBegin] << 8) + (BYTE)data[nIndex+3-compBlockVirtBegin];
	}



protected:
	int compBufIndex;
	ULONG compBlockSize;
	ULONG compBlockVirtBegin;
	ULONG compBlockVirtEnd;
	vector< pair<BYTE*, long> > compBlocks;
};
