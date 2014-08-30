#ifdef _WIN32
	#include "stdafx.h"
#endif

#include "common.h"
#include "Root.h"
#include "VGMFile.h"
#include "Format.h"

DECLARE_MENU(VGMFile)

using namespace std;

VGMFile::VGMFile(FileType fileType, const string& fmt, RawFile* theRawFile, uint32_t offset, uint32_t length, wstring theName)
: VGMContainerItem(this, offset, length),
  rawfile(theRawFile),
  bUsingRawFile(true),
  bUsingCompressedLocalData(false),
  format(fmt),
  file_type(fileType),
  name(theName),
  id(-1)
{
}

VGMFile::~VGMFile(void)
{

}

// Only difference between this AddToUI and VGMItemContainer's version is that we do not add
// this as an item because we do not want the VGMFile to be itself an item in the Item View
void VGMFile::AddToUI(VGMItem* parent, void* UI_specific)
{
	for (uint32_t i=0; i<containers.size(); i++)
	{
		for (uint32_t j=0; j<containers[i]->size(); j++)
			(*containers[i])[j]->AddToUI(this, UI_specific);
	}
}

// All we do is remove "return this", because we do not want to select the entire file when we click
// an unspecified region of the file.
VGMItem* VGMFile::GetItemFromOffset(uint32_t offset)
{
	if (IsItemAtOffset(offset))				//if the offset is within this item
	{
		for (uint32_t i=0; i<containers.size(); i++)
		{
			for (uint32_t j=0; j<containers[i]->size(); j++)	
			{
				VGMItem* foundItem = (*containers[i])[j]->GetItemFromOffset(offset);
				if (foundItem)
					return foundItem;
			}
		}
	}
	return NULL;
}


//void VGMFile::kill(void)
//{
//	delete this;
//}

bool VGMFile::OnClose()
{
	pRoot->RemoveVGMFile(this);
	return true;
}

bool VGMFile::OnSaveAsRaw()
{
	wstring filepath = pRoot->UI_GetSaveFilePath(name.c_str());
	if (filepath.length() != 0)
	{
		bool result;
		uint8_t* buf = new uint8_t[unLength];		//create a buffer the size of the file
		GetBytes(dwOffset, unLength, buf);
		result = pRoot->UI_WriteBufferToFile(filepath.c_str(), buf, unLength);
		delete[] buf;
		return result;
	}
	return false;
}

bool VGMFile::OnSaveAllAsRaw()
{
	return pRoot->SaveAllAsRaw();
}

bool VGMFile::LoadVGMFile()
{
	bool val = Load();
	if (!val)
		return false;
	Format* fmt = GetFormat();
	if (fmt)
		fmt->OnNewFile(this);

	pRoot->AddLogItem(new LogItem(wstring(L"Loaded \"" + name + L"\" successfully.").c_str(), LOG_LEVEL_INFO, L"VGMFile"));
	return val;
}

Format* VGMFile::GetFormat()
{
	return Format::GetFormatFromName(format);
}

const string& VGMFile::GetFormatName()
{
	return format;
}


const wstring* VGMFile::GetName(void) const
{
	return &name;
}

void VGMFile::AddCollAssoc(VGMColl* coll)
{
	assocColls.push_back(coll);
}

void VGMFile::RemoveCollAssoc(VGMColl* coll)
{
	list<VGMColl*>::iterator iter = find(assocColls.begin(), assocColls.end(), coll);
	if (iter != assocColls.end())
		assocColls.erase(iter);
}

//void VGMFile::AddItem(VGMItem* item, VGMItem* parent, const wchar_t* itemName)
//{
//	if (itemName == NULL)
//	{
//		itemName = item->name;
//		if (itemName == NULL)
//			itemName = L"unnamed item";
//	}
//	pRoot->UI_AddItem(this, item, parent, itemName);
//}
//
//void VGMFile::AddItemSet(vector<ItemSet>* itemset)
//{
//	pRoot->UI_AddItemSet(this, itemset);
//}


//These functions are common to all VGMItems, but no reason to refer to vgmfile
//or call GetRawFile() if the item itself is a VGMFile
RawFile* VGMFile::GetRawFile()
{
	return rawfile;
}

void VGMFile::LoadLocalData()
{
	assert(unLength < 1024*1024*256); //sanity check... we're probably not allocating more than 256mb
	data.clear();
	data.alloc(unLength);
	rawfile->GetBytes(dwOffset, unLength, data.data);
	data.startOff = dwOffset;
	data.endOff = dwOffset+unLength;
	data.size = unLength;
}

void VGMFile::UseLocalData()
{
	bUsingRawFile = false;
}

void VGMFile::UseRawFileData()
{
	bUsingRawFile = true;
}



uint32_t VGMFile::GetBytes(uint32_t nIndex, uint32_t nCount, void* pBuffer)
{
	// if unLength != 0, verify that we're within the bounds of the file, and truncate num read bytes to end of file
	if (unLength != 0)
	{
		uint32_t endOff = dwOffset+unLength;
		assert (nIndex >= dwOffset && nIndex < endOff);
		if (nIndex + nCount > endOff)
			nCount = endOff - nIndex; 
	}

	if (bUsingRawFile)
		return rawfile->GetBytes(nIndex, nCount, pBuffer);
	else
	{
		if ((nIndex + nCount) > data.endOff)
			nCount = data.endOff - nIndex;

		assert(nIndex >= data.startOff && (nIndex+nCount <= data.endOff));
		memcpy(pBuffer, data.data+nIndex-data.startOff, nCount);
		return nCount;
	}
}



// *********
// VGMHeader
// *********

VGMHeader::VGMHeader(VGMItem* parItem, uint32_t offset, uint32_t length, const wchar_t* name)
: VGMContainerItem(parItem->vgmfile, offset, length, name)
{
	//AddContainer<VGMItem>(items);
}

VGMHeader::~VGMHeader()
{
	//DeleteVect<VGMItem>(items);
}

void VGMHeader::AddPointer(uint32_t offset, uint32_t length, uint32_t destAddress, bool notNull, const wchar_t *name)
{
	localitems.push_back(new VGMHeaderItem(this, VGMHeaderItem::HIT_POINTER, offset, length, name));
}

void VGMHeader::AddTempo(uint32_t offset, uint32_t length, const wchar_t *name)
{
	localitems.push_back(new VGMHeaderItem(this, VGMHeaderItem::HIT_TEMPO, offset, length, name));
}

void VGMHeader::AddSig(uint32_t offset, uint32_t length, const wchar_t *name)
{
	localitems.push_back(new VGMHeaderItem(this, VGMHeaderItem::HIT_SIG, offset, length, name));
}

// *************
// VGMHeaderItem
// *************

VGMHeaderItem::VGMHeaderItem(VGMHeader* hdr, HdrItemType theType, uint32_t offset, uint32_t length, const wchar_t* name)
: VGMItem(hdr->vgmfile, offset, length, name, CLR_HEADER), type(theType)
{
}

VGMItem::Icon VGMHeaderItem::GetIcon()
{
	switch (type)
	{
	case HIT_UNKNOWN:
		return ICON_UNKNOWN;
	case HIT_POINTER:
		return ICON_BINARY;
	case HIT_TEMPO:
		return ICON_TEMPO;
	case HIT_SIG:
		return ICON_BINARY;
	default:
		return ICON_BINARY;
	}
}