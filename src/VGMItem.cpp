#include "stdafx.h"
#include "VGMItem.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "Root.h"

VGMItem::VGMItem()
: color(0)
{
}

VGMItem::VGMItem(VGMFile* thevgmfile, ULONG theOffset, ULONG theLength, const wstring theName, BYTE theColor)
: vgmfile(thevgmfile),
  name(theName),
  dwOffset(theOffset),
  unLength(theLength),
  color(theColor)
{
}

VGMItem::~VGMItem(void)
{
}

bool operator> (VGMItem &item1, VGMItem &item2)
{
	return item1.dwOffset > item2.dwOffset;
}

bool operator<= (VGMItem &item1, VGMItem &item2)
{
	return item1.dwOffset <= item2.dwOffset;
}

bool operator< (VGMItem &item1, VGMItem &item2)
{
	return item1.dwOffset < item2.dwOffset;
}

bool operator>= (VGMItem &item1, VGMItem &item2)
{
	return item1.dwOffset >= item2.dwOffset;
}


RawFile* VGMItem::GetRawFile()
{
	return vgmfile->rawfile;
}


VGMItem* VGMItem::GetItemFromOffset(ULONG offset)
{
	if (IsItemAtOffset(offset))
		return this;
	return NULL;
}

void VGMItem::AddToUI(VGMItem* parent, VOID* UI_specific)
{
	pRoot->UI_AddItem(this, parent, name, UI_specific);
}

/*
void SetOffset(ULONG newOffset)
{
	dwOffset = newOffset;
}

void SetLength(ULONG newLength)
{
	unLength = newLength;
}*/

UINT VGMItem::GetBytes(UINT nIndex, UINT nCount, void* pBuffer)
{
	return vgmfile->GetBytes(nIndex, nCount, pBuffer);
}

BYTE VGMItem::GetByte(ULONG offset)
{
	return vgmfile->GetByte(offset);
}

USHORT VGMItem::GetShort(ULONG offset)
{
	return vgmfile->GetShort(offset);
}

UINT VGMItem::GetWord(ULONG offset)
{
	return GetRawFile()->GetWord(offset);
}

//GetShort Big Endian
USHORT VGMItem::GetShortBE(ULONG offset)
{
	return GetRawFile()->GetShortBE(offset);
}

//GetWord Big Endian
UINT VGMItem::GetWordBE(ULONG offset)
{
	return GetRawFile()->GetWordBE(offset);
}



//  ****************
//  VGMContainerItem
//  ****************

VGMContainerItem::VGMContainerItem()
: VGMItem()
{
	AddContainer(headers);
	AddContainer(localitems);
}


VGMContainerItem::VGMContainerItem(VGMFile* thevgmfile, ULONG theOffset, ULONG theLength, const wstring theName, BYTE color)
: VGMItem(thevgmfile, theOffset, theLength, theName, color)
{
	AddContainer(headers);
	AddContainer(localitems);
}

VGMContainerItem::~VGMContainerItem()
{
	DeleteVect(headers);
	DeleteVect(localitems);
}

VGMItem* VGMContainerItem::GetItemFromOffset(ULONG offset)
{
	if (IsItemAtOffset(offset))				//if the offset is within this item
	{
		for (UINT i=0; i<containers.size(); i++)
		{
			for (UINT j=0; j<containers[i]->size(); j++)	
			{
				VGMItem* foundItem = (*containers[i])[j]->GetItemFromOffset(offset);
				if (foundItem)
					return foundItem;
			}
		}
		return NULL; // this offset must be a "hole", so that it should return nothing
	}
	return NULL;
}

void VGMContainerItem::AddToUI(VGMItem* parent, VOID* UI_specific)
{
	VGMItem::AddToUI(parent, UI_specific);
	for (UINT i=0; i<containers.size(); i++)
	{
		for (UINT j=0; j<containers[i]->size(); j++)
			(*containers[i])[j]->AddToUI(this, UI_specific);
	}
}

VGMHeader* VGMContainerItem::AddHeader(ULONG offset, ULONG length, const wchar_t* name)
{
	VGMHeader* header = new VGMHeader(this, offset, length, name);
	headers.push_back(header);
	return header;
}

void VGMContainerItem::AddItem(VGMItem* item)
{
	localitems.push_back(item);
}

void VGMContainerItem::AddSimpleItem(ULONG offset, ULONG length, const wchar_t *name)
{
	localitems.push_back(new VGMItem(this->vgmfile, offset, length, name, CLR_HEADER));
	//items.push_back(new VGMHeaderItem(this, VGMHeaderItem::HIT_GENERIC, offset, length, name));
}

void VGMContainerItem::AddUnknownItem(ULONG offset, ULONG length)
{
	localitems.push_back(new VGMItem(this->vgmfile, offset, length, L"Unknown"));
	//items.push_back(new VGMHeaderItem(this, VGMHeaderItem::HIT_UNKNOWN, offset, length, L"Unknown"));
}

/*
void VGMContainerItem::AddHeaderItem(VGMItem* item)
{
	header->AddItem(item);
}
	
void VGMContainerItem::AddSimpleHeaderItem(ULONG offset, ULONG length, const wchar_t* name)
{
	header->AddSimpleItem(offset, length, name);
}
*/


//VGMContainerItem::~VGMContainerItem(void)
//{
//	//for (UINT i=0; i<containers.size(); i++)
//	//	DeleteVect<VGMItem>(*containers[i]);
//		//DeleteVect<VGMItem>(*containers[i]);
//}
