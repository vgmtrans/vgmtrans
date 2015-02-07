#include "stdafx.h"
#include "DataSeg.h"
#include <zlib.h>


DataSeg::DataSeg(void)
: startOff(0), size(0), endOff(0), bAlloced(false)
{
}

DataSeg::~DataSeg(void)
{
	clear();
}

void DataSeg::reposition(uint32_t newBegin)
{
	if (newBegin < endOff && newBegin >= startOff)
	{
		uint32_t diff = newBegin-startOff;
		memmove(data, data+diff, size-diff);
	}
	else if (newBegin+size <= endOff && newBegin+size >= startOff)
	{
		uint32_t diff = endOff - (newBegin+size);
		memmove(data+diff, data, size-diff);
	}
	startOff = newBegin;
	endOff = startOff+size;
}

void DataSeg::alloc(uint32_t theSize)
{
	assert(!bAlloced);

	if (bAlloced)
	{
		// alloc() does not save the existing data.
		delete[] data;
	}

	data = new uint8_t[theSize > 0 ? theSize : 1];
	size = theSize;
	bAlloced = true;
}

void DataSeg::load(uint8_t* buf, uint32_t startVirtOffset, uint32_t theSize)
{
	clear();

	data = buf;
	startOff = startVirtOffset;
	size = theSize;
	endOff = startOff+size;
	bAlloced = true;
}


void DataSeg::clear()
{
	if (bAlloced)
		delete[] data;
	//data.clear();
	startOff = 0;
	size = 0;
	endOff = 0;
	bAlloced = false;
}


//bool AttemptExpand(uint32_t offset)
//{

CompDataSeg::CompDataSeg(void)
: DataSeg(), compBlockVirtBegin(-1), compBlockVirtEnd(-1)
{
}

CompDataSeg::~CompDataSeg(void)
{
	clear();
}

void CompDataSeg::UpdateCompBuf(uint32_t offset)
{
	assert((offset >= startOff) && (offset < endOff));
	int newCompBufIndex = (offset-startOff)/compBlockSize;
	if (newCompBufIndex == compBufIndex)
		return;

	compBufIndex = newCompBufIndex;
	assert((unsigned)compBufIndex < compBlocks.size());
	uLongf destLen = compBlockSize;
	uncompress(data, &destLen, compBlocks[compBufIndex].first, compBlocks[compBufIndex].second);
	compBlockVirtBegin = compBufIndex*compBlockSize + startOff;
	compBlockVirtEnd = compBlockVirtBegin + compBlockSize;
}



void CompDataSeg::load(uint8_t* buf, uint32_t startVirtOffset, uint32_t theSize, uint32_t compBufSize)
{
	clear();
	int nSegments = theSize / compBufSize;
	if (theSize % compBufSize)
		nSegments++;
	if (compBufSize > theSize)
		compBlockSize = theSize;
	else
		compBlockSize = compBufSize;
	data = new uint8_t[compBlockSize];	

	//clear out the comp blocks, if they were previously used
	

	compBlocks.reserve(nSegments);
	uint32_t offset = startVirtOffset;
	for (int i = 0; i < nSegments; i++)
	{
		if (offset + compBufSize > startVirtOffset+theSize)
			compBufSize = startVirtOffset+theSize - offset;
		uLongf destSize = (uLongf)(compBufSize * 1.2 + 12);
		uint8_t* compBlock = (uint8_t*)malloc(destSize);
		
		compress(compBlock, &destSize, buf+(compBlockSize*i), compBufSize);
		compBlock = (uint8_t*)realloc(compBlock, destSize);
		compBlocks.push_back(std::pair<uint8_t*, long>(compBlock, destSize));
		offset += compBufSize;
	}
	//memcpy(data, buf, theSize);
	compBufIndex = -1;
	startOff = startVirtOffset;
	size = theSize;
	endOff = startOff+size;
	bAlloced = true;
}


void CompDataSeg::clear()
{
	if (!bAlloced)
		return;
	for (uint32_t i=0; i<compBlocks.size(); i++)
		free(compBlocks[i].first);
	compBlocks.clear();
	delete[] data;
	bAlloced = false;
}