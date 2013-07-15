#pragma once

#include "common.h"

typedef enum EventColors {
	CLR_UNKNOWN,
	CLR_UNRECOGNIZED,
	CLR_HEADER,
	CLR_MISC,
	CLR_MARKER,
	CLR_TIMESIG,
	CLR_TEMPO,
	CLR_PROGCHANGE,
	CLR_TRANSPOSE,
	CLR_PRIORITY,
	CLR_VOLUME,
	CLR_EXPRESSION,
	CLR_PAN,
	CLR_NOTEON,
	CLR_NOTEOFF,
	CLR_DURNOTE,
	CLR_TIE,
	CLR_REST,
	CLR_PITCHBEND,
	CLR_PITCHBENDRANGE,
	CLR_MODULATION,
	CLR_PORTAMENTO,
	CLR_PORTAMENTOTIME,
	CLR_CHANGESTATE,
	CLR_ADSR,
	CLR_LFO,
	CLR_REVERB,
	CLR_SUSTAIN,
	CLR_LOOP,
	CLR_LOOPFOREVER,
	CLR_TRACKEND,
};

/*
#define TEXT_CLR_BLACK 0
#define TEXT_CLR_RED 0x20
#define TEXT_CLR_PURPLE 0x40
#define TEXT_CLR_BLUE 0x60

#define BG_CLR_WHITE 0
#define BG_CLR_RED 1
#define BG_CLR_GREEN 2
#define BG_CLR_BLUE 3
#define BG_CLR_YELLOW 4
#define BG_CLR_MAGENTA 5
#define BG_CLR_CYAN 6
#define BG_CLR_DARKRED 7
#define BG_CLR_BLACK 8
#define BG_CLR_DARKBLUE 9
#define BG_CLR_STEEL 10
#define BG_CLR_CHEDDAR 11
#define BG_CLR_PINK 12
#define BG_CLR_LIGHTBLUE 13
#define BG_CLR_AQUA 14
#define BG_CLR_PEACH 15
#define BG_CLR_WHEAT 16

#define CLR_UNKNOWN			(BG_CLR_BLACK|TEXT_CLR_RED)
#define CLR_NOTEON			BG_CLR_BLUE
#define CLR_NOTEOFF			BG_CLR_DARKBLUE
#define CLR_DURNOTE			CLR_NOTEON
#define CLR_REST			CLR_NOTEOFF
#define CLR_PROGCHANGE		BG_CLR_PINK
#define CLR_VOLUME			BG_CLR_CYAN
#define CLR_EXPRESSION		BG_CLR_CYAN
#define CLR_PAN				BG_CLR_MAGENTA
#define CLR_PITCHBEND		BG_CLR_PEACH
#define CLR_PITCHBENDRANGE	BG_CLR_PEACH
#define CLR_TRANSPOSE		BG_CLR_GREEN
#define CLR_TIMESIG			BG_CLR_CHEDDAR
#define CLR_MODULATION		BG_CLR_PEACH
#define CLR_TEMPO			BG_CLR_AQUA
#define CLR_SUSTAIN			BG_CLR_WHEAT
#define CLR_PORTAMENTO		BG_CLR_LIGHTBLUE
#define CLR_PORTAMENTOTIME	BG_CLR_LIGHTBLUE
#define CLR_TRACKEND		BG_CLR_RED
#define CLR_LOOPFOREVER		BG_CLR_CHEDDAR
#define CLR_HEADER			BG_CLR_YELLOW*/

template <class T> class Menu;

class RawFile;
class VGMFile;
class VGMItem;
class VGMHeader;

typedef struct _ItemSet
{
	VGMItem* item;
	VGMItem* parent;
	const wchar_t* itemName;
} ItemSet;

enum ItemType { ITEMTYPE_UNDEFINED, ITEMTYPE_VGMFILE, ITEMTYPE_SEQEVENT };

class VGMItem
{
public:
	enum Icon { ICON_SEQ, ICON_INSTRSET, ICON_SAMPCOLL, ICON_UNKNOWN, ICON_NOTE, ICON_TRACK, ICON_REST, ICON_CONTROL,
		ICON_STARTREP, ICON_ENDREP, ICON_TIMESIG, ICON_TEMPO, ICON_PROGCHANGE, ICON_TRACKEND, ICON_COLL,
		ICON_INSTR, ICON_SAMP, ICON_BINARY, MAX }; 

public:
	VGMItem();
	VGMItem(VGMFile* thevgmfile, ULONG theOffset, ULONG theLength = 0, const wstring theName = L"", BYTE color = 0);
	virtual ~VGMItem(void);

	friend bool operator>	(VGMItem &item1, VGMItem &item2);
	friend bool operator<= (VGMItem &item1, VGMItem &item2); 
	friend bool operator<	(VGMItem &item1, VGMItem &item2);
	friend bool operator>= (VGMItem &item1, VGMItem &item2);

public:
	//inline bool IsItemAtOffset(ULONG offset);
	inline bool IsItemAtOffset(ULONG offset)
	{
		if ((offset >= dwOffset) && (offset < dwOffset+unLength))
			return true;
		else
			return false;
	}
	virtual VGMItem* GetItemFromOffset(ULONG offset);

	RawFile* GetRawFile();

	//inline void SetOffset(ULONG newOffset);
	//inline void SetLength(ULONG newLength);

	virtual vector<const wchar_t*>* GetMenuItemNames() { return NULL; }
	virtual bool CallMenuItem(VGMItem* item, int menuItemNum) { return false; }
	virtual wstring GetDescription() { return name; }
	virtual ItemType GetType() const { return ITEMTYPE_UNDEFINED; }
	virtual Icon GetIcon() { return ICON_BINARY;/*ICON_UNKNOWN*/ }
	virtual void AddToUI(VGMItem* parent, VOID* UI_specific);
	virtual bool IsContainerItem() { return false; }

	//bool AddHeader(ULONG offset, ULONG length, const wchar_t* name = L"Header");
	//bool VGMHeader(VGMItem* parItem, ULONG offset, ULONG length, const wchar_t* name);

protected:
	//TODO make inline
	UINT GetBytes(UINT nIndex, UINT nCount, void* pBuffer);
	BYTE GetByte(ULONG offset);
	USHORT GetShort(ULONG offset);
	UINT GetWord(ULONG offset);	
	USHORT GetShortBE(ULONG offset);
	UINT GetWordBE(ULONG offset);

public:
	BYTE color;
	//RawFile* file;
	VGMFile* vgmfile;
	wstring name;
	//VGMHeader* header;
	//VGMItem* parent;
	ULONG dwOffset;			//offset in the pDoc data buffer
	ULONG unLength;			//num of bytes the event engulfs
};



class VGMContainerItem
	: public VGMItem
{
public:
	VGMContainerItem();
	VGMContainerItem(VGMFile* thevgmfile, ULONG theOffset, ULONG theLength = 0, const wstring theName = L"", BYTE color = CLR_HEADER);
	virtual ~VGMContainerItem(void);
	virtual VGMItem* GetItemFromOffset(ULONG offset);
	virtual void AddToUI(VGMItem* parent, VOID* UI_specific);
	virtual bool IsContainerItem() { return true; }

	VGMHeader* AddHeader(ULONG offset, ULONG length, const wchar_t* name = L"Header");

	void AddItem(VGMItem* item);
	void AddSimpleItem(ULONG offset, ULONG length, const wchar_t *theName);
	void AddUnknownItem(ULONG offset, ULONG length);

	//void AddHeaderItem(VGMItem* item);
	//void AddSimpleHeaderItem(ULONG offset, ULONG length, const wchar_t* name);
	

	template <class T> void AddContainer(vector<T*>& container)
	{
		containers.push_back((vector<VGMItem*>*)&container);
	}
	template <class T> bool RemoveContainer(vector<T*>& container)
	{
		vector<vector<VGMItem*>*>::iterator iter = find(containers.begin(),
			containers.end(), (vector<VGMItem*>*)&container);
		if (iter != containers.end())
		{
			containers.erase(iter);
			return true;
		}
		else
			return false;
	}
public:
	vector<VGMHeader*> headers;
	vector<vector<VGMItem*>*> containers;

	vector<VGMItem*> localitems;
	//vector<void*> containers;
};




class ItemPtrOffsetCmp
{
public:
	bool operator() (const VGMItem* a, const VGMItem* b) const
	{
		return (a->dwOffset < b->dwOffset);
	}
};


template <class T> VGMItem* GetItemAtOffsetInItemVector(ULONG offset, vector<T*> &theArray)
{
	int nArraySize = (int)theArray.size ();
	for (int i=0; i<nArraySize; i++)
	{
		if (((VGMItem*)theArray[i])->IsItemAtOffset(offset))
			return theArray[i];
	}
	return NULL;
}