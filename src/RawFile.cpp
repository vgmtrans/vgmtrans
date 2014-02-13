#include "stdafx.h"

#include "RawFile.h"
#include "common.h"
#include "VGMFile.h"
#include "Root.h"

using namespace std;

#define BUF_SIZE 0x100000		//1mb

RawFile::RawFile(void)
: propreRatio(0.5),
  processFlags(PF_USESCANNERS | PF_USELOADERS),
  parRawFileFullPath(L""),
  bCanFileRead(true),
  fileSize(0)
{
	bufSize = (BUF_SIZE > fileSize) ? fileSize : BUF_SIZE;
}

RawFile::RawFile(const wstring name, ULONG theFileSize, bool bCanRead)
: fileSize(theFileSize), /*data(databuf),*/
  //filename(name),
  fullpath(name),
  parRawFileFullPath(L""),		//this should only be defined by VirtFile
  propreRatio(0.5),
  bCanFileRead(bCanRead),
  processFlags(PF_USESCANNERS | PF_USELOADERS)
{
	filename = getFileNameFromPath(fullpath);
	extension = getExtFromPath(fullpath);
	//col = new unsigned char[filesize];
	//memset(col, 0, filesize);
}


RawFile::~RawFile(void)
{
	pRoot->UI_BeginRemoveVGMFiles();
	int size = containedVGMFiles.size();
	for (int i=0; i<size; i++)
	{
		pRoot->RemoveVGMFile(containedVGMFiles.front(), false);
		containedVGMFiles.erase(containedVGMFiles.begin());
	}
	//for (list<VGMFile*>::iterator iter = containedVGMFiles.begin(); iter != containedVGMFiles.end(); iter = containedVGMFiles.begin())
	//	pRoot->RemoveVGMFile(*iter, false);
	//containedVGMFiles.clear();
	pRoot->UI_EndRemoveVGMFiles();

	pRoot->UI_CloseRawFile(this);
	//pRoot->RemoveVGMFileRange(containedVGMFiles.front(), containedVGMFiles.back());
	//delete[] data;
	//delete[] col;
}

// opens a file using the standard c++ file i/o routines
bool RawFile::open(const wstring& theFileName)
{
#if _MSC_VER < 1400			//if we're not using VC8, and the new STL that supports widechar filenames in ofstream...

	char newpath[_MAX_PATH];
	wcstombs(newpath, theFileName, _MAX_PATH);
	file.open(newpath,  ios::in | ios::binary);
#else
	file.open(theFileName,  ios::in | ios::binary);
#endif
	if (!file.is_open())
	{
		Alert(L"File %s could not be opened", theFileName);
		return false;
	}

	//fileSize = theFile.tellg();
	// get pointer to associated buffer object
	pbuf=file.rdbuf();

	 // get file size using buffer's members
	fileSize=(ULONG)pbuf->pubseekoff(0,ios::end,ios::in);
	//pbuf->pubseekpos (0,ios::in);

	// allocate memory to contain file data
	//buffer=new char[size];

	// get file data  
	//pbuf->sgetn (buffer,size);

	//data = new unsigned char[filesize];
	//pbuf->sgetn ((char*)data,filesize);
	//col = new unsigned char[filesize];
	//memset(col, 0, filesize);
	//if (!theFile.read(buf, fileSize))
	//{
	//	theFile.close();
	//	return false;
	//}


	bufSize = (BUF_SIZE > fileSize) ? fileSize : BUF_SIZE;
	buf.alloc(bufSize);
	return true;
}

// closes the file
void RawFile::close()
{
	file.close();
}

// returns the size of the file
unsigned long RawFile::size(void)
{
	return fileSize;
}

// Name says it all.
wstring RawFile::getFileNameFromPath(const wstring& s)
{
	size_t i = s.rfind('/', s.length( ));
	size_t j = s.rfind('\\', s.length( ));
	if (i == string::npos || (j != string::npos && i < j))
		i = j;
	if (i != string::npos)
	{
		return(s.substr(i+1, s.length( ) - i));
	}
	return s;
}

wstring RawFile::getExtFromPath(const wstring& s)
{
	size_t i = s.rfind('.', s.length( ));
	if (i != string::npos)
	{
		return(s.substr(i+1, s.length( ) - i));
	}
	return(_T(""));
}

wstring RawFile::removeExtFromPath(const wstring& s)
{
	size_t i = s.rfind('.', s.length( ));
	if (i != string::npos)
	{
		return(s.substr(0, i));
	}
	return s;
}


// Sets the  "ProPre" ratio.  Terrible name, yes.  Every time the buffer is updated
// from an attempt to read at an offset beyond its current bounds, the propre ratio determines
// the new bounds of the buffer.  Literally, it is a ratio determining how much of the bounds should be
// ahead of the current offset.  If, for example, the ratio were 0.7, 70% of the buffer would contain data
// ahead of the requested offset and 30% would be below it.
void RawFile::SetProPreRatio(float newRatio)
{
	if (newRatio > 1 || newRatio < 0)
		return;
	propreRatio = newRatio;
}

// given an offset, determines which, if any, VGMItem is encompasses the offset
VGMItem* RawFile::GetItemFromOffset(long offset)
{
	for (list<VGMFile*>::iterator iter = containedVGMFiles.begin(); iter != containedVGMFiles.end(); iter++)
	{
		VGMItem* item = (*iter)->GetItemFromOffset(offset);
		if (item != NULL)
			return item;
	}
	return NULL;
}

// given an offset, determines which, if any, VGMFile encompasses the offset.
VGMFile* RawFile::GetVGMFileFromOffset(long offset)
{
	for (list<VGMFile*>::iterator iter = containedVGMFiles.begin(); iter != containedVGMFiles.end(); iter++)
	{
		if ((*iter)->IsItemAtOffset(offset))
			return *iter;
	}
	return NULL;
}
	
// adds the association of a VGMFile to this RawFile
void RawFile::AddContainedVGMFile(VGMFile* vgmfile)
{
	containedVGMFiles.push_back(vgmfile);
}

// removes the association of a VGMFile with this RawFile
void RawFile::RemoveContainedVGMFile(VGMFile* vgmfile)
{
	list<VGMFile*>::iterator iter = find(containedVGMFiles.begin(), containedVGMFiles.end(), vgmfile);
	if (iter != containedVGMFiles.end())
		containedVGMFiles.erase(iter);
	else
		OutputDebugString(L"Error: trying to delete a vgmfile which cannot be found in containedVGMFiles.");
	if (containedVGMFiles.size() == 0)
		pRoot->CloseRawFile(this);
}

// read data directly from the file
int RawFile::FileRead(void* dest, ULONG index, ULONG length)
{
	assert(bCanFileRead);
	assert(index+length <= fileSize);
	pbuf->pubseekpos(index, ios::in);
	return (int)pbuf->sgetn((char*)dest, length);	//return bool value based on whether we read all requested bytes
	//dest->insert(index, temp, length);
	//copy(temp, temp+length, dest->data.begin() + index - dest->startOff);
}


/*
//the rules for updating the buffer are pretty simple.  UpdateBuffer gets called whenever there
//is a read attempt that is beyond the current range of the buffer.  There is a max size
//of the buffer (buf_size).  UpdateBuffer makes sure that we're not trying to read more from the buffer
//than is allowed (I might want to allow for this in future)... TODO finish the explanation
void RawFile::UpdateBuffer(ULONG index, ULONG size)
{
	LONG beginOffset = 0;
	LONG endOffset = 0;
	ULONG beginLength = 0;
	ULONG endLength = 0;

	assert(bCanFileRead);
	assert(index+size <= fileSize);		//better not ask for data beyond the length of the file
	assert(size <= buf_size);			//the size can't be greater than the max buf size (could handle this differently)
	//ULONG buf_size = buf_size;
	if (buf_size > fileSize)
		buf_size = fileSize;

	if (size/2 > buf_size)
	{
		//ULONG padding = (buf_size - size) / 2;
		//if (padding <= index && index+size)	//if the padding is > index, then we'd get a negative offset if we subtracted it
		beginOffset = index;
		endOffset = index+buf_size;
		if (endOffset > fileSize)
			endOffset = fileSize;
	}
	else
	{
		LONG originOff;
		if (index < buf.startOff)
			originOff = index;
		else if (index+size > buf.endOff)
			originOff = index+size;

		beginOffset = originOff-(buf_size/2);
		if (beginOffset < 0)
		{
			//endLength += negate(beginOffset);
			endOffset += 0-beginOffset;
			beginOffset = 0;
		}
		endOffset += originOff+(buf_size/2);
		if (endOffset > fileSize)
		{
			//beginOffset -= endOffset - fileSize;
			endOffset = fileSize;
		}
	}

	if (beginOffset < buf.startOff)
	{
		beginLength = buf.startOff-beginOffset;
		if (beginLength > buf_size)
			beginLength = buf_size;
	}
	if (endOffset > buf.endOff)
	{
		endLength = endOffset - buf.endOff;
		if (endLength > buf_size)
			endLength = buf_size;
	}

	ULONG origbufEndOff;
	if (buf.endOff < beginOffset)
		origbufEndOff = beginOffset;
	else
		origbufEndOff = buf.endOff;
	buf.resize(beginOffset, endOffset-beginOffset);
	if (beginLength)
		FileRead(&buf, beginOffset, beginLength);
	if (endLength)
		FileRead(&buf, origbufEndOff, endLength);
}

*/


/*
UINT RawFile::GetBytes(UINT nIndex, UINT nCount, void* pBuffer)
{
    //assert(nIndex+nCount < filesize);
	if ((nIndex + nCount) > fileSize)
        nCount = fileSize - nIndex;

	if ((nIndex < buf.startOff) || (nIndex+nCount > buf.endOff))
		UpdateBuffer(nIndex, nCount);
    
	UINT realIndex = nIndex-buf.startOff;
	copy<deque<BYTE>::iterator, BYTE*>(buf.data.begin()+realIndex, buf.data.begin()+realIndex+nCount, (BYTE*)pBuffer);
	//memcpy(pBuffer, &buf[nIndex], nCount);
	return nCount;
}*/

// reads a bunch of data from a given offset.  If the requested data goes beyond the bounds
// of the file buffer, the buffer is updated.  If the requested size is greater than the buffer size
// a direct read from the file is executed.
UINT RawFile::GetBytes(UINT nIndex, UINT nCount, void* pBuffer)
{
	if ((nIndex + nCount) > fileSize)
        nCount = fileSize - nIndex;

	if (nCount > buf.size)
		FileRead(pBuffer, nIndex, nCount);
	else
	{
		if ((nIndex < buf.startOff) || (nIndex+nCount > buf.endOff))
			UpdateBuffer(nIndex);

		memcpy(pBuffer, buf.data+nIndex-buf.startOff, nCount);
	}
	return nCount;
}

// attempts to match the data from a given offset against a given pattern.
// If the requested data goes beyond the bounds of the file buffer, the buffer is updated.
// If the requested size is greater than the buffer size, it always fails. (operation not supported)
bool RawFile::MatchBytePattern(const BytePattern& pattern, UINT nIndex)
{
	UINT nCount = pattern.length();

	if ((nIndex + nCount) > fileSize)
		return false;

	if (nCount > buf.size)
		return false; // not supported
	else
	{
		if ((nIndex < buf.startOff) || (nIndex+nCount > buf.endOff))
			UpdateBuffer(nIndex);

		return pattern.match(buf.data+nIndex-buf.startOff, nCount);
	}
}

bool RawFile::SearchBytePattern(const BytePattern& pattern, UINT& nMatchOffset, UINT nSearchOffset, UINT nSearchSize)
{
	if (nSearchOffset >= fileSize)
		return false;

	if ((nSearchOffset + nSearchSize) > fileSize)
		nSearchSize = fileSize - nSearchOffset;

	if (nSearchSize < pattern.length())
		return false;

	for (UINT nIndex = nSearchOffset; nIndex < nSearchOffset + nSearchSize - pattern.length(); nIndex++)
	{
		if (MatchBytePattern(pattern, nIndex))
		{
			nMatchOffset = nIndex;
			return true;
		}
	}
	return false;
}

/*
int RawFile::FileRead(DataSeg* dest, ULONG index, ULONG length)
{
	assert(index+length <= fileSize);
	pbuf->pubseekpos(index, ios::in);
	BYTE* temp = new BYTE[length];
	pbuf->sgetn((char*)temp, length);	//return bool value based on whether we read all requested bytes
	//dest->insert(index, temp, length);
	copy(temp, temp+length, dest->data.begin() + index - dest->startOff);
	delete[] temp;
	return true;
}
*/

// Given a requested offset, fills the buffer with data surrounding that offset.  The ratio
// of how much data is read before and after the offset is determined by the ProPre ratio (explained above).
void RawFile::UpdateBuffer(ULONG index)
{
	ULONG beginOffset = 0;
	ULONG endOffset = 0;

	assert(bCanFileRead);

	if (!buf.bAlloced)
		buf.alloc(bufSize);

	ULONG proBytes = (ULONG)(buf.size*propreRatio);
	ULONG preBytes = buf.size-proBytes;
	if (proBytes+index > fileSize)
	{
		preBytes += (proBytes+index)-fileSize;
		proBytes = fileSize-index;	//make it go just to the end of the file;
	}
	else if (preBytes > index)
	{
		proBytes += preBytes - index;
		preBytes = index;
	}
	beginOffset = index-preBytes;
	endOffset = index+proBytes;

	if (beginOffset >= buf.startOff && beginOffset < buf.endOff)
	{
		ULONG prevEndOff = buf.endOff;
		buf.reposition(beginOffset);
		FileRead(buf.data+prevEndOff-buf.startOff, prevEndOff, endOffset - prevEndOff);
	}
	else if (endOffset >= buf.startOff && endOffset < buf.endOff)
	{
		ULONG prevStartOff = buf.startOff;
		buf.reposition(beginOffset);
		FileRead(buf.data, beginOffset, prevStartOff - beginOffset);
	}
	else
	{
		FileRead(buf.data, beginOffset, buf.size);
		buf.startOff = beginOffset;
		buf.endOff = beginOffset + buf.size;
	}
}

bool RawFile::OnSaveAsRaw()
{
	wstring filepath = pRoot->UI_GetSaveFilePath(filename.c_str());
	if (filepath.length() != 0)
	{
		bool result;
		BYTE* buf = new BYTE[fileSize];		//create a buffer the size of the file
		GetBytes(0, fileSize, buf);
		result = pRoot->UI_WriteBufferToFile(filepath.c_str(), buf, fileSize);
		delete[] buf;
		return result;
	}
	return false;
}









//  ********
//  VirtFile
//  ********

VirtFile::VirtFile()
: RawFile()
{
}

VirtFile::VirtFile(BYTE* data, ULONG fileSize, const wstring& name, const wchar_t* rawFileName)
: RawFile(name, fileSize, false)
{
	parRawFileFullPath = rawFileName;
	buf.load(data, 0, fileSize);
	//buf.insert_back(data, fileSize);
}