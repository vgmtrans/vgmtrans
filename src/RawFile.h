#pragma once
#include "common.h"
#include "DataSeg.h"
#include "BytePattern.h"

class VGMFile;
class VGMItem;

enum ProcessFlag {
	PF_USELOADERS = 1,
	PF_USESCANNERS = 2
};

class RawFile
{
public:
	RawFile(void);
	RawFile(const std::wstring name, ULONG fileSize = 0, bool bCanRead = true);
public:
	virtual ~RawFile(void);

//	void kill(void);

	bool open(const std::wstring& filename);
	void close();
	unsigned long size(void);
	inline const wchar_t* GetFullPath() { return fullpath.c_str(); }
	inline const wchar_t* GetFileName() { return filename.c_str(); }	//returns the filename with extension
	inline const std::wstring& GetExtension() { return extension; }
	inline const std::wstring& GetParRawFileFullPath() { return parRawFileFullPath; }
	static std::wstring getFileNameFromPath(const std::wstring& s);
	static std::wstring getExtFromPath(const std::wstring& s);
	static std::wstring removeExtFromPath(const std::wstring& s);
	VGMItem* GetItemFromOffset(long offset);
	VGMFile* GetVGMFileFromOffset(long offset);

	//UINT GetColors(UINT nIndex, UINT nCount, void* pBuffer);
	//UINT SetColor(UINT nIndex, UINT nCount, BYTE color);
	virtual int FileRead(void* dest, ULONG index, ULONG length);

	void UpdateBuffer(ULONG index);

	float GetProPreRatio(void) { return propreRatio; }
	void SetProPreRatio(float newRatio);

	inline BYTE& operator[](ULONG offset)
	{
		if ((offset < buf.startOff) || (offset >= buf.endOff))
			UpdateBuffer(offset);
		return buf[offset];
	}

	
	inline BYTE GetByte(UINT nIndex)
	{
		if ((nIndex < buf.startOff) || (nIndex+1 > buf.endOff))
			UpdateBuffer(nIndex);
		return buf[nIndex];
	}

	inline USHORT GetShort(UINT nIndex)
	{
		if ((nIndex < buf.startOff) || (nIndex+2 > buf.endOff))
			UpdateBuffer(nIndex);
		return buf.GetShort(nIndex);
	}

	inline UINT GetWord(UINT nIndex)
	{
		if ((nIndex < buf.startOff) || (nIndex+4 > buf.endOff))
			UpdateBuffer(nIndex);
		return buf.GetWord(nIndex);
	}

	inline USHORT GetShortBE(UINT nIndex)
	{
		if ((nIndex < buf.startOff) || (nIndex+2 > buf.endOff))
			UpdateBuffer(nIndex);
		return buf.GetShortBE(nIndex);
	}

	inline UINT GetWordBE(UINT nIndex)
	{
		if ((nIndex < buf.startOff) || (nIndex+4 > buf.endOff))
			UpdateBuffer(nIndex);
		return buf.GetWordBE(nIndex);
	}

	inline bool IsValidOffset(UINT nIndex)
	{
		return (nIndex < fileSize);
	}

	inline void UseLoaders() { processFlags |= PF_USELOADERS; }
	inline void DontUseLoaders() { processFlags &= ~PF_USELOADERS; }
	inline void UseScanners() { processFlags |= PF_USESCANNERS; }
	inline void DontUseScanners() { processFlags &= ~PF_USESCANNERS; }

	UINT GetBytes(UINT nIndex, UINT nCount, void* pBuffer);
	//BYTE GetByte(UINT nIndex);
	//USHORT GetShort(UINT nIndex);
	//USHORT GetShortBE(UINT nIndex);
	//UINT GetWord(UINT nIndex);
	//UINT GetWordBE(UINT nIndex);
	bool MatchBytePattern(const BytePattern& pattern, UINT nIndex);
	bool SearchBytePattern(const BytePattern& pattern, UINT& nMatchOffset, UINT nSearchOffset = 0, UINT nSearchSize = (UINT)-1);

	void AddContainedVGMFile(VGMFile* vgmfile);
	void RemoveContainedVGMFile(VGMFile* vgmfile);

	bool OnSaveAsRaw();

public:
	DataSeg buf;
	ULONG bufSize;
	float propreRatio;
	BYTE processFlags;
	//ULONG buf_size;
	//unsigned char *data;
	//unsigned char *col;

protected:
	std::ifstream file;
	std::filebuf *pbuf;
	bool bCanFileRead;
	unsigned long fileSize;
	std::wstring fullpath;
	std::wstring filename;
	std::wstring extension;
	std::wstring parRawFileFullPath;
public:
	std::list<VGMFile*> containedVGMFiles;
};


class VirtFile : public RawFile
{
public:
	VirtFile();
	VirtFile(BYTE* data, ULONG fileSize, const std::wstring& name, const wchar_t* parRawFileFullPath = L"");
};