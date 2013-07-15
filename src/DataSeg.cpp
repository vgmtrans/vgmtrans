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

void DataSeg::reposition(ULONG newBegin)
{
	if (newBegin < endOff && newBegin >= startOff)
	{
		ULONG diff = newBegin-startOff;
		memmove(data, data+diff, size-diff);
	}
	else if (newBegin+size <= endOff && newBegin+size >= startOff)
	{
		ULONG diff = endOff - (newBegin+size);
		memmove(data+diff, data, size-diff);
	}
	startOff = newBegin;
	endOff = startOff+size;
}

void DataSeg::alloc(ULONG theSize)
{
	assert(!bAlloced);
	data = new BYTE[theSize];
	//memset(data, defValue, theSize);
	size = theSize;
	bAlloced = true;
}

void DataSeg::load(BYTE* buf, ULONG startVirtOffset, ULONG theSize)
{
	/*if (!bAlloced)
	{
		data = new BYTE[theSize];
		size = theSize;
	}
	else
		assert(theSize <= size);*/

	data = buf;
	//memcpy(data, buf, theSize);
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


//bool AttemptExpand(ULONG offset)
//{

CompDataSeg::CompDataSeg(void)
: DataSeg(), compBlockVirtBegin(-1), compBlockVirtEnd(-1)
{
}

CompDataSeg::~CompDataSeg(void)
{
	clear();
}

void CompDataSeg::UpdateCompBuf(ULONG offset)
{
	assert((offset >= startOff) && (offset < endOff));
	int newCompBufIndex = (offset-startOff)/compBlockSize;
	if (newCompBufIndex == compBufIndex)
		return;

	compBufIndex = newCompBufIndex;
	assert(compBufIndex < compBlocks.size());
	ULONG destLen = compBlockSize;
	uncompress(data, &destLen, compBlocks[compBufIndex].first, compBlocks[compBufIndex].second);
	compBlockVirtBegin = compBufIndex*compBlockSize + startOff;
	compBlockVirtEnd = compBlockVirtBegin + compBlockSize;
}



void CompDataSeg::load(BYTE* buf, ULONG startVirtOffset, ULONG theSize, ULONG compBufSize)
{
	clear();
	int nSegments = theSize / compBufSize;
	if (theSize % compBufSize)
		nSegments++;
	if (compBufSize > theSize)
		compBlockSize = theSize;
	else
		compBlockSize = compBufSize;
	data = new BYTE[compBlockSize];	

	//clear out the comp blocks, if they were previously used
	

	compBlocks.reserve(nSegments);
	ULONG offset = startVirtOffset;
	for (int i = 0; i < nSegments; i++)
	{
		if (offset + compBufSize > startVirtOffset+theSize)
			compBufSize = startVirtOffset+theSize - offset;
		ULONG destSize = (ULONG)(compBufSize * 1.2 + 12);
		BYTE* compBlock = (BYTE*)malloc(destSize);
		
		compress(compBlock, &destSize, buf+(compBlockSize*i), compBufSize);
		compBlock = (BYTE*)realloc(compBlock, destSize);
		compBlocks.push_back(pair<BYTE*, long>(compBlock, destSize));
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
	for (UINT i=0; i<compBlocks.size(); i++)
		free(compBlocks[i].first);
	compBlocks.clear();
	delete[] data;
	bAlloced = false;
}