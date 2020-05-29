/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <utility>

#include "VGMFile.h"
#include "Root.h"

VGMItem::VGMItem(VGMFile *thevgmfile, uint32_t theOffset, uint32_t theLength, std::string theName,
                 uint8_t theColor)
        : vgmfile(thevgmfile),
          name(std::move(theName)),
          dwOffset(theOffset),
          unLength(theLength),
          color(theColor) {}

VGMItem::~VGMItem() = default;

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

RawFile *VGMItem::GetRawFile() const {
    return vgmfile->rawfile;
}

bool VGMItem::IsItemAtOffset(uint32_t offset, bool includeContainer, bool matchStartOffset) {
    return GetItemFromOffset(offset, includeContainer, matchStartOffset) != nullptr;
}

VGMItem *VGMItem::GetItemFromOffset(uint32_t offset, bool includeContainer, bool matchStartOffset) {
    if ((matchStartOffset ? offset == dwOffset : offset >= dwOffset) &&
        (offset < dwOffset + unLength)) {
        return this;
    } else {
        return nullptr;
    }
}

uint32_t VGMItem::GuessLength() {
    return unLength;
}

void VGMItem::SetGuessedLength() {
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
    return GetRawFile()->GetWord(offset);
}

// GetShort Big Endian
uint16_t VGMItem::GetShortBE(uint32_t offset) const {
    return GetRawFile()->GetShortBE(offset);
}

// GetWord Big Endian
uint32_t VGMItem::GetWordBE(uint32_t offset) const {
    return GetRawFile()->GetWordBE(offset);
}

bool VGMItem::IsValidOffset(uint32_t offset) const {
    return vgmfile->IsValidOffset(offset);
}

//  ****************
//  VGMContainerItem
//  ****************

VGMContainerItem::VGMContainerItem(VGMFile *thevgmfile, uint32_t theOffset, uint32_t theLength,
                                   const std::string &theName, uint8_t color)
        : VGMItem(thevgmfile, theOffset, theLength, theName, color) {
    AddContainer(headers);
    AddContainer(localitems);
}

VGMContainerItem::~VGMContainerItem() {
    DeleteVect(headers);
    DeleteVect(localitems);
}

VGMItem *VGMContainerItem::GetItemFromOffset(uint32_t offset, bool includeContainer,
                                             bool matchStartOffset) {
    for (auto &container : containers) {
        for (auto theItem : *container) {
            if (theItem->unLength == 0 ||
                (offset >= theItem->dwOffset && offset < theItem->dwOffset + theItem->unLength)) {
                VGMItem *foundItem =
                        theItem->GetItemFromOffset(offset, includeContainer, matchStartOffset);
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

    // Note: children items can sometimes overwrap each other
    for (auto &container : containers) {
        for (auto item : *container) {
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
    for (auto &container : containers) {
        for (auto item : *container) {
            item->SetGuessedLength();
        }
    }

    if (unLength == 0) {
        unLength = GuessLength();
    }
}

void VGMContainerItem::AddToUI(VGMItem *parent, void *UI_specific) {
    VGMItem::AddToUI(parent, UI_specific);
    for (auto &container : containers) {
        for (auto &j : *container)
            j->AddToUI(this, UI_specific);
    }
}

VGMHeader *VGMContainerItem::AddHeader(uint32_t offset, uint32_t length, const std::string &name) {
    auto *header = new VGMHeader(this, offset, length, name);
    headers.push_back(header);
    return header;
}

void VGMContainerItem::AddItem(VGMItem *item) {
    localitems.push_back(item);
}

void VGMContainerItem::AddSimpleItem(uint32_t offset, uint32_t length, const std::string &name) {
    localitems.push_back(new VGMItem(this->vgmfile, offset, length, name, CLR_HEADER));
}

void VGMContainerItem::AddUnknownItem(uint32_t offset, uint32_t length) {
    localitems.push_back(new VGMItem(this->vgmfile, offset, length, "Unknown"));
}
