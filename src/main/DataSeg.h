#pragma once

#include "pch.h"
#include <deque>

//DataSeg is a very simple class for allocating a block of data with a
//variable reference point for indexing.  Some formats use
//absolute offset references (the Nintendo SNES seq format, for example)

class DataSeg
{
public:
	DataSeg(void);
	~DataSeg(void);

	inline uint8_t& operator[](uint32_t offset)
	{
		assert(offset >= startOff && (offset < (startOff+size)));
		//if(offset >= startOff && (offset < (startOff+size)))
			return data[offset-startOff];
		//else
		//	assert(AttemptExpand(offset));
	}

	//bool AttemptExpand(uint32_t offset);
	//void insert_front(uint8_t* buf, uint32_t size);
	//void insert(uint32_t index, uint8_t* buf, uint32_t size);
	//void insert_back(uint8_t* buf, uint32_t size);
	//void copy(
	//void resize(uint32_t newStartOff, uint32_t newSize);
public:
	void reposition(uint32_t newBegin);
	void load(uint8_t* buf, uint32_t startVirtAddr, uint32_t theSize);
	void alloc(uint32_t theSize);
	void clear();


	inline void GetBytes(uint32_t nIndex, uint32_t nCount, void* pBuffer)
	{
		assert((nIndex >= startOff) && (nIndex+nCount <= endOff));
		memcpy(pBuffer, data+nIndex-startOff, nCount);
	}

	inline uint8_t GetByte(uint32_t nIndex)
	{
		assert((nIndex >= startOff) && (nIndex+1 <= endOff));
		return data[nIndex-startOff];
	}

	inline uint16_t GetShort(uint32_t nIndex)
	{
		assert((nIndex >= startOff) && (nIndex+2 <= endOff));
		return *((uint16_t*)(data+nIndex-startOff));
	}

	inline uint32_t GetWord(uint32_t nIndex)
	{
		assert((nIndex >= startOff) && (nIndex+4 <= endOff));
		return *((uint32_t*)(data+nIndex-startOff));
	}

	inline uint16_t GetShortBE(uint32_t nIndex)
	{
		assert((nIndex >= startOff) && (nIndex+2 <= endOff));
		return ((uint8_t)(data[nIndex-startOff]) << 8) + ((uint8_t)data[nIndex+1-startOff]);
	}

	inline uint32_t GetWordBE(uint32_t nIndex)
	{
		assert((nIndex >= startOff) && (nIndex+4 <= endOff));
		return ((uint8_t)data[nIndex-startOff] << 24) + ((uint8_t)data[nIndex+1-startOff] << 16)
			 + ((uint8_t)data[nIndex+2-startOff] << 8) + (uint8_t)data[nIndex+3-startOff];
	}

	inline bool IsValidOffset(uint32_t nIndex)
	{
		return (nIndex >= startOff && nIndex < endOff);
	}


public:
	//UCHAR *data;
	//deque<uint8_t> data;
	uint8_t* data;

	uint32_t startOff;
	uint32_t endOff;
	uint32_t size;
	bool bAlloced;
};


class CompDataSeg : public DataSeg
{
public:
	CompDataSeg(void);
	~CompDataSeg(void);

	void load(uint8_t* buf, uint32_t startVirtAddr, uint32_t theSize, uint32_t compBufSize);
	void clear();

	
	void UpdateCompBuf(uint32_t offset);

	inline uint8_t& operator[](uint32_t offset)
	{
		if (offset < compBlockVirtBegin || offset >= compBlockVirtEnd)
			UpdateCompBuf(offset);
		return data[offset-compBlockVirtBegin];
	}

	inline void GetBytes(uint32_t nIndex, uint32_t nCount, void* pBuffer)
	{
		if ((nIndex + nCount) > endOff)
			nCount = endOff - nIndex;
		if (nCount < 0)
			nCount = 0;
		//int nSegments = (nIndex+nCount-compBlockVirtBegin) / compBufSize;
		//if ((nIndex+nCount-compBlockVirtBegin) % compBufSize)
		//	nSegments++;
		uint32_t curOffset = 0;
		while (nCount)
		{
			if (nIndex < compBlockVirtBegin || nIndex >= compBlockVirtEnd)
				UpdateCompBuf(nIndex);
			uint32_t transferSize;
			if (nCount > compBlockSize)
				transferSize = compBlockSize;
			else
				transferSize = nCount;
			if (nIndex + transferSize > compBlockVirtEnd)
				transferSize = compBlockVirtEnd-nIndex;
			memcpy((uint8_t*)pBuffer+curOffset, (uint8_t*)data+nIndex-compBlockVirtBegin, transferSize);
			nCount -= transferSize;
			nIndex += transferSize;
			curOffset += transferSize;
		}
	}

	inline uint8_t GetByte(uint32_t nIndex)
	{
		return data[nIndex-startOff];
	}

	inline uint16_t GetShort(uint32_t nIndex)
	{
		//assert((nIndex >= startOff) && (nIndex <= endOff));
		if (nIndex < compBlockVirtBegin || nIndex >= compBlockVirtEnd)
			UpdateCompBuf(nIndex);
		return *((uint16_t*)(data+nIndex-compBlockVirtBegin));
	}

	inline uint32_t GetWord(uint32_t nIndex)
	{
		if (nIndex < compBlockVirtBegin || nIndex >= compBlockVirtEnd)
			UpdateCompBuf(nIndex);
		return *((uint32_t*)(data+nIndex-compBlockVirtBegin));
	}

	inline uint16_t GetShortBE(uint32_t nIndex)
	{
		if (nIndex < compBlockVirtBegin || nIndex >= compBlockVirtEnd)
			UpdateCompBuf(nIndex);
		return ((uint8_t)(data[nIndex-compBlockVirtBegin]) << 8) + ((uint8_t)data[nIndex+1-compBlockVirtBegin]);
	}

	inline uint32_t GetWordBE(uint32_t nIndex)
	{
		if (nIndex < compBlockVirtBegin || nIndex >= compBlockVirtEnd)
			UpdateCompBuf(nIndex);
		return ((uint8_t)data[nIndex-compBlockVirtBegin] << 24) + ((uint8_t)data[nIndex+1-compBlockVirtBegin] << 16)
			 + ((uint8_t)data[nIndex+2-compBlockVirtBegin] << 8) + (uint8_t)data[nIndex+3-compBlockVirtBegin];
	}



protected:
	int compBufIndex;
	uint32_t compBlockSize;
	uint32_t compBlockVirtBegin;
	uint32_t compBlockVirtEnd;
	std::vector< std::pair<uint8_t*, long> > compBlocks;
};
