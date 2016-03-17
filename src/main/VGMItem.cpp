#include "pch.h"
#include "VGMItem.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "Root.h"

using namespace std;

VGMItem::VGMItem()
    : color(0) {
}

VGMItem::VGMItem(VGMFile *thevgmfile, uint32_t theOffset, uint32_t theLength, const wstring theName, uint8_t theColor)
    : vgmfile(thevgmfile),
      name(theName),
      dwOffset(theOffset),
      unLength(theLength),
      color(theColor) {
}

VGMItem::~VGMItem(void) {
}

bool operator>(VGMItem &item1, VGMItem &item2) {
  return item1.dwOffset > item2.dwOffset;
}

bool operator<=(VGMItem &item1, VGMItem &item2) {
  return item1.dwOffset <= item2.dwOffset;
}

bool operator<(VGMItem &item1, VGMItem &item2) {
  return item1.dwOffset < item2.dwOffset;
}

bool operator>=(VGMItem &item1, VGMItem &item2) {
  return item1.dwOffset >= item2.dwOffset;
}


RawFile *VGMItem::GetRawFile() {
  return vgmfile->rawfile;
}


bool VGMItem::IsItemAtOffset(uint32_t offset, bool includeContainer, bool matchStartOffset) {
  if (GetItemFromOffset(offset, includeContainer, matchStartOffset) != NULL) {
    return true;
  }
  else {
    return false;
  }
}

VGMItem *VGMItem::GetItemFromOffset(uint32_t offset, bool includeContainer, bool matchStartOffset) {
  if ((matchStartOffset ? offset == dwOffset : offset >= dwOffset) && (offset < dwOffset + unLength)) {
    return this;
  }
  else {
    return NULL;
  }
}

uint32_t VGMItem::GuessLength(void) {
  return unLength;
}

void VGMItem::SetGuessedLength(void) {
  return;
}

void VGMItem::AddToUI(VGMItem *parent, void *UI_specific) {
  pRoot->UI_AddItem(this, parent, name, UI_specific);
}

uint32_t VGMItem::GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) {
  return vgmfile->GetBytes(nIndex, nCount, pBuffer);
}

uint8_t VGMItem::GetByte(uint32_t offset) {
  return vgmfile->GetByte(offset);
}

uint16_t VGMItem::GetShort(uint32_t offset) {
  return vgmfile->GetShort(offset);
}

uint32_t VGMItem::GetWord(uint32_t offset) {
  return GetRawFile()->GetWord(offset);
}

//GetShort Big Endian
uint16_t VGMItem::GetShortBE(uint32_t offset) {
  return GetRawFile()->GetShortBE(offset);
}

//GetWord Big Endian
uint32_t VGMItem::GetWordBE(uint32_t offset) {
  return GetRawFile()->GetWordBE(offset);
}

bool VGMItem::IsValidOffset(uint32_t offset) {
  return vgmfile->IsValidOffset(offset);
}



//  ****************
//  VGMContainerItem
//  ****************

VGMContainerItem::VGMContainerItem()
    : VGMItem() {
  AddContainer(headers);
  AddContainer(localitems);
}


VGMContainerItem::VGMContainerItem(VGMFile *thevgmfile,
                                   uint32_t theOffset,
                                   uint32_t theLength,
                                   const wstring theName,
                                   uint8_t color)
    : VGMItem(thevgmfile, theOffset, theLength, theName, color) {
  AddContainer(headers);
  AddContainer(localitems);
}

VGMContainerItem::~VGMContainerItem() {
  DeleteVect(headers);
  DeleteVect(localitems);
}

VGMItem *VGMContainerItem::GetItemFromOffset(uint32_t offset, bool includeContainer, bool matchStartOffset) {
  for (uint32_t i = 0; i < containers.size(); i++) {
    for (uint32_t j = 0; j < containers[i]->size(); j++) {
      VGMItem *theItem = (*containers[i])[j];
      if (theItem->unLength == 0 || (offset >= theItem->dwOffset && offset < theItem->dwOffset + theItem->unLength)) {
        VGMItem *foundItem = theItem->GetItemFromOffset(offset, includeContainer, matchStartOffset);
        if (foundItem)
          return foundItem;
      }
    }
  }

  if (includeContainer && (matchStartOffset ? offset == dwOffset : offset >= dwOffset)
      && (offset < dwOffset + unLength)) {
    return this;
  }
  else {
    return NULL;
  }
}

// Guess length of a container from its descendants
uint32_t VGMContainerItem::GuessLength(void) {
  uint32_t guessedLength = 0;

  // Note: children items can sometimes overwrap each other
  for (uint32_t i = 0; i < containers.size(); i++) {
    for (uint32_t j = 0; j < containers[i]->size(); j++) {
      VGMItem *item = (*containers[i])[j];

      assert(dwOffset <= item->dwOffset);

      uint32_t itemLength = item->unLength;
      if (unLength == 0) {
        itemLength = item->GuessLength();
      }

      uint32_t expectedLength = item->dwOffset + itemLength - dwOffset;
      if (guessedLength < expectedLength) {
        guessedLength = expectedLength;
      }
    }
  }

  return guessedLength;
}

void VGMContainerItem::SetGuessedLength(void) {
  for (uint32_t i = 0; i < containers.size(); i++) {
    for (uint32_t j = 0; j < containers[i]->size(); j++) {
      VGMItem *item = (*containers[i])[j];
      item->SetGuessedLength();
    }
  }

  if (unLength == 0) {
    unLength = GuessLength();
  }
}

void VGMContainerItem::AddToUI(VGMItem *parent, void *UI_specific) {
  VGMItem::AddToUI(parent, UI_specific);
  for (uint32_t i = 0; i < containers.size(); i++) {
    for (uint32_t j = 0; j < containers[i]->size(); j++)
      (*containers[i])[j]->AddToUI(this, UI_specific);
  }
}

VGMHeader *VGMContainerItem::AddHeader(uint32_t offset, uint32_t length, const std::wstring &name) {
  VGMHeader *header = new VGMHeader(this, offset, length, name);
  headers.push_back(header);
  return header;
}

void VGMContainerItem::AddItem(VGMItem *item) {
  localitems.push_back(item);
}

void VGMContainerItem::AddSimpleItem(uint32_t offset, uint32_t length, const std::wstring &name) {
  localitems.push_back(new VGMItem(this->vgmfile, offset, length, name, CLR_HEADER));
}

void VGMContainerItem::AddUnknownItem(uint32_t offset, uint32_t length) {
  localitems.push_back(new VGMItem(this->vgmfile, offset, length, L"Unknown"));
}
