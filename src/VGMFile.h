#pragma once
#include "VGMItem.h"
#include "DataSeg.h"
#include "RawFile.h"
#include "Menu.h"

extern enum FmtID;
class VGMColl;
class Format;

enum FileType { FILETYPE_UNDEFINED, FILETYPE_SEQ, FILETYPE_INSTRSET, FILETYPE_SAMPCOLL, FILETYPE_MISC };


// MACROS

/*#define USING_FORMAT(fmt_id)									\
	public:														\
	virtual Format* GetFormat()									\
	{															\
		return pRoot->GetFormat(fmt_id);						\
	}
*/



class VGMFile :
	public VGMContainerItem
{
public:
	BEGIN_MENU(VGMFile)
		MENU_ITEM(VGMFile, OnClose, L"Close")
		MENU_ITEM(VGMFile, OnSaveAsRaw, L"Save as original format")
		//MENU_ITEM(VGMFile, OnSaveAllAsRaw, L"Save all as original format")
	END_MENU()

public:
	VGMFile(FileType fileType, /*FmtID fmtID,*/const string& format, RawFile* theRawFile, ULONG offset, ULONG length = 0, wstring theName = L"VGM File");
	virtual ~VGMFile(void);

	virtual ItemType GetType() const { return ITEMTYPE_VGMFILE; }
	FileType GetFileType() { return file_type; }
	virtual VGMItem* GetItemFromOffset(ULONG offset);

	virtual void AddToUI(VGMItem* parent, VOID* UI_specific);

	const wstring* GetName(void) const;
	//void AddItem(VGMItem* item, VGMItem* parent, const wchar_t* itemName = NULL);
	//void AddItemSet(vector<ItemSet>* itemset);

	bool OnClose();
	bool OnSaveAsRaw();
	bool OnSaveAllAsRaw();

	bool LoadVGMFile();
	virtual bool Load() = 0;
	//virtual Format* GetFormat() = 0;  // {return 0;}
	Format* GetFormat();
	const string& GetFormatName();

	virtual ULONG GetID() { return id;}
	//virtual void Announce() {}
	//virtual VGMItem* GetItemFromOffset(ULONG offset) = 0;

	void AddCollAssoc(VGMColl* coll);
	void RemoveCollAssoc(VGMColl* coll);
	RawFile* GetRawFile();
	void LoadLocalData();
	void UseLocalData();
	void UseRawFileData();

public:
	UINT GetBytes(UINT nIndex, UINT nCount, void* pBuffer);
		
	inline BYTE GetByte(ULONG offset)
	{
		if (bUsingRawFile)
			return rawfile->GetByte(offset);
		else
			return data[offset];
	}

	inline USHORT GetShort(ULONG offset)
	{
		//if (nIndex+1 >= filesize)
		//	return 0;

		//return *((USHORT*)(data+nIndex));
		if (bUsingRawFile)
			return rawfile->GetShort(offset);
		else
			return data.GetShort(offset);
	}

	inline UINT GetWord(ULONG offset)
	{
		if (bUsingRawFile)
			return rawfile->GetWord(offset);
		else
			return data.GetWord(offset);
	}

	//GetShort Big Endian
	inline USHORT GetShortBE(ULONG offset)
	{
		if (bUsingRawFile)
			return rawfile->GetShortBE(offset);
		else
			return data.GetShortBE(offset);
	}

	//GetWord Big Endian
	inline UINT GetWordBE(ULONG offset)
	{
		if (bUsingRawFile)
			return rawfile->GetWordBE(offset);
		else
			return data.GetWordBE(offset);
	}
		

protected:
	DataSeg data, col;
	FileType file_type;
	const string& format; 
	//FmtID fmt_id;
	ULONG id;
	wstring name;
public:
	RawFile* rawfile;
	list<VGMColl*> assocColls;
	bool bUsingRawFile;
	bool bUsingCompressedLocalData;
};






// *********
// VGMHeader
// *********

class VGMHeader :
	public VGMContainerItem
{
public:
	VGMHeader(VGMItem* parItem, ULONG offset = 0, ULONG length = 0, const wchar_t* name = L"Header");
	virtual ~VGMHeader();

	virtual Icon GetIcon() { return ICON_BINARY; };

	void AddPointer(ULONG offset, ULONG length, ULONG destAddress, bool notNull, const wchar_t *name = L"Pointer");
	void AddTempo(ULONG offset, ULONG length, const wchar_t *name = L"Tempo");
	void AddSig(ULONG offset, ULONG length, const wchar_t *name = L"Signature");

	//vector<VGMItem*> items;
};

// *************
// VGMHeaderItem
// *************

class VGMHeaderItem :
	public VGMItem
{
public:
	enum HdrItemType { HIT_POINTER, HIT_TEMPO, HIT_SIG, HIT_GENERIC, HIT_UNKNOWN };		//HIT = Header Item Type

	VGMHeaderItem(VGMHeader* hdr, HdrItemType theType, ULONG offset, ULONG length, const wchar_t* name);
	virtual Icon GetIcon();

public:
	HdrItemType type;
};
