#include "pch.h"
#include "VGMItem.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "Root.h"

using namespace std;

VGMItem::VGMItem() : color(CLR_UNKNOWN) {
}

VGMItem::VGMItem(VGMFile *vgmfile, uint32_t offset, uint32_t length, const wstring name, EventColor color)
    : vgmfile(vgmfile), name(name), dwOffset(offset), unLength(length), color(color) {
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
  return GetItemFromOffset(offset, includeContainer, matchStartOffset) != nullptr;
}

VGMItem *VGMItem::GetItemFromOffset(uint32_t offset, bool includeContainer, bool matchStartOffset) {
  if ((matchStartOffset ? offset == dwOffset : offset >= dwOffset) && (offset < dwOffset + unLength)) {
    return this;
  } else {
    return nullptr;
  }
}

void VGMItem::AddToUI(VGMItem *parent, void *UI_specific) {
  pRoot->UI_AddItem(this, parent, name, UI_specific);
}

uint32_t VGMItem::GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) const {
  return vgmfile->GetBytes(nIndex, nCount, pBuffer);
}

uint8_t VGMItem::GetByte(uint32_t offset) const {
  return vgmfile->GetByte(offset);
}

uint16_t VGMItem::GetShort(uint32_t offset) const {
  return vgmfile->GetShort(offset);
}

uint32_t VGMItem::GetWord(uint32_t offset) const {
  return vgmfile->GetWord(offset);
}

// GetShort Big Endian
uint16_t VGMItem::GetShortBE(uint32_t offset) const {
  return vgmfile->GetShortBE(offset);
}

// GetWord Big Endian
uint32_t VGMItem::GetWordBE(uint32_t offset) const {
  return vgmfile->GetWordBE(offset);
}

bool VGMItem::IsValidOffset(uint32_t offset) const {
  return vgmfile->IsValidOffset(offset);
}

//  ****************
//  VGMContainerItem
//  ****************

VGMContainerItem::VGMContainerItem() : VGMItem() {
  AddContainer(headers);
  AddContainer(localitems);
}

VGMContainerItem::VGMContainerItem(
    VGMFile *vgmfile, uint32_t offset, uint32_t length, const wstring name, EventColor color)
    : VGMItem(vgmfile, offset, length, name, color) {
  AddContainer(headers);
  AddContainer(localitems);
}

VGMContainerItem::~VGMContainerItem() {
  DeleteVect(headers);
  DeleteVect(localitems);
}

VGMItem *VGMContainerItem::GetItemFromOffset(uint32_t offset, bool includeContainer, bool matchStartOffset) {
  for (const auto *container : containers) {
    for (VGMItem *item : *container) {
      if (item->unLength == 0 || (offset >= item->dwOffset && offset < item->dwOffset + item->unLength)) {
        VGMItem *foundItem = item->GetItemFromOffset(offset, includeContainer, matchStartOffset);
        if (foundItem)
          return foundItem;
      }
    }
  }

  if (includeContainer && (matchStartOffset ? offset == dwOffset : offset >= dwOffset) &&
      (offset < dwOffset + unLength)) {
    return this;
  } else {
    return nullptr;
  }
}

// Guess length of a container from its descendants
uint32_t VGMContainerItem::GuessLength() {
  uint32_t guessedLength = 0;

  // NOTE: children items can sometimes overlap each other
  for (const auto *container : containers) {
    for (VGMItem *item : *container) {
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

void VGMContainerItem::SetGuessedLength() {
  for (const auto *container : containers) {
    for (VGMItem *item : *container) {
      item->SetGuessedLength();
    }
  }

  if (unLength == 0) {
    unLength = GuessLength();
  }
}

void VGMContainerItem::AddToUI(VGMItem *parent, void *UI_specific) {
  VGMItem::AddToUI(parent, UI_specific);
  for (const auto *container : containers) {
    for (VGMItem *item : *container) {
      item->AddToUI(this, UI_specific);
    }
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
