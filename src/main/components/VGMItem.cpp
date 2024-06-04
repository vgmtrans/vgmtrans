#include "VGMItem.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "Root.h"
#include "helper.h"

VGMItem::VGMItem() : m_vgmfile(nullptr), dwOffset(0), unLength(0), color(CLR_UNKNOWN) {
}

VGMItem::VGMItem(VGMFile *vgmfile, uint32_t offset, uint32_t length, std::string name, EventColor color)
    : m_vgmfile(vgmfile), m_name(std::move(name)), dwOffset(offset), unLength(length), color(color) {
}

bool operator>(const VGMItem &item1, const VGMItem &item2) {
  return item1.dwOffset > item2.dwOffset;
}

bool operator<=(const VGMItem &item1, const VGMItem &item2) {
  return item1.dwOffset <= item2.dwOffset;
}

bool operator<(const VGMItem &item1, const VGMItem &item2) {
  return item1.dwOffset < item2.dwOffset;
}

bool operator>=(const VGMItem &item1, const VGMItem &item2) {
  return item1.dwOffset >= item2.dwOffset;
}

RawFile *VGMItem::rawFile() const {
  return m_vgmfile->rawFile();
}

bool VGMItem::IsItemAtOffset(uint32_t offset, bool matchStartOffset) {
  return GetItemFromOffset(offset, matchStartOffset) != nullptr;
}

VGMItem* VGMItem::GetItemFromOffset(uint32_t offset, bool matchStartOffset) {
  for (const auto child : m_children) {
    if (VGMItem *foundItem = child->GetItemFromOffset(offset, matchStartOffset))
      return foundItem;
  }

  if ((matchStartOffset ? offset == dwOffset : offset >= dwOffset) && (offset < dwOffset + unLength)) {
    return this;
  }
  return nullptr;
}

VGMItem* VGMItem::GetItemFromOffset(uint32_t offset, bool matchStartOffset, std::function<bool(const VGMItem*)> filterFunc) {
  for (const auto& child : m_children) {
    if (VGMItem* foundItem = child->GetItemFromOffset(offset, matchStartOffset, filterFunc)) {
      return foundItem;
    }
  }

  if (!filterFunc(this)) {
    if ((matchStartOffset ? offset == dwOffset : offset >= dwOffset) && (offset < dwOffset + unLength)) {
      return this;
    }
  }
  return nullptr;
}

// VGMItem* VGMItem::GetItemFromOffset(uint32_t offset, bool matchStartOffset, const std::unordered_set<std::type_index>& filterTypes) {
//   for (const auto& child : m_children) {
//     if (VGMItem* foundItem = child->GetItemFromOffset(offset, matchStartOffset, filterTypes))
//       return foundItem;
//   }
//
//   // Check if the current type is in the filter list
//   if (filterTypes.find(typeid(*this)) == filterTypes.end()) {
//     if ((matchStartOffset ? offset == dwOffset : offset >= dwOffset) && (offset < dwOffset + unLength)) {
//       return this;
//     }
//   }
//   return nullptr;
// }

// VGMItem* VGMItem::GetItemFromOffset(uint32_t offset, bool matchStartOffset,
//                                    const std::unordered_set<std::type_index>& filterTypes = {}) {
//   for (const auto& child : m_children) {
//     if (VGMItem* foundItem = child->GetItemFromOffset(offset, matchStartOffset, filterTypes))
//       return foundItem;
//   }
//
//   if (!isFilteredType(this, filterTypes)) {
//     if ((matchStartOffset ? offset == dwOffset : offset >= dwOffset) && (offset < dwOffset + unLength)) {
//       return this;
//     }
//   }
//   return nullptr;
// }

void VGMItem::AddToUI(VGMItem *parent, void *UI_specific) {
  pRoot->UI_AddItem(this, parent, name(), UI_specific);
  for (const auto child : m_children) {
    pRoot->UI_AddItem(child, this, name(), UI_specific);
  }
}

uint32_t VGMItem::GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) const {
  return m_vgmfile->GetBytes(nIndex, nCount, pBuffer);
}

uint8_t VGMItem::GetByte(uint32_t offset) const {
  return m_vgmfile->GetByte(offset);
}

uint16_t VGMItem::GetShort(uint32_t offset) const {
  return m_vgmfile->GetShort(offset);
}

uint32_t VGMItem::GetWord(uint32_t offset) const {
  return m_vgmfile->GetWord(offset);
}

// GetShort Big Endian
uint16_t VGMItem::GetShortBE(uint32_t offset) const {
  return m_vgmfile->GetShortBE(offset);
}

// GetWord Big Endian
uint32_t VGMItem::GetWordBE(uint32_t offset) const {
  return m_vgmfile->GetWordBE(offset);
}

bool VGMItem::IsValidOffset(uint32_t offset) const {
  return m_vgmfile->IsValidOffset(offset);
}

VGMItem* VGMItem::addChild(VGMItem *item) {
  m_children.emplace_back(item);
  return item;
}

VGMItem* VGMItem::addSimpleChild(uint32_t offset, uint32_t length, const std::string &name) {
  auto child = new VGMItem(vgmFile(), offset, length, name, CLR_HEADER);
  m_children.emplace_back(child);
  return child;
}

VGMItem* VGMItem::addUnknownChild(uint32_t offset, uint32_t length) {
  auto child = new VGMItem(vgmFile(), offset, length, "Unknown");
  m_children.emplace_back(child);
  return child;
}

VGMHeader* VGMItem::addHeader(uint32_t offset, uint32_t length, const std::string &name) {
  auto *header = new VGMHeader(this, offset, length, name);
  m_children.emplace_back(header);
  return header;
}

// Guess length of a container from its descendants
uint32_t VGMItem::GuessLength() {
  if (m_children.empty()) {
    return unLength;
  }
  // NOTE: child items can sometimes overlap each other
  uint32_t guessedLength = 0;
  for (const auto child : m_children) {
    assert(dwOffset <= child->dwOffset);

    uint32_t itemLength = child->unLength;
    if (unLength == 0) {
      itemLength = child->GuessLength();
    }

    uint32_t expectedLength = child->dwOffset + itemLength - dwOffset;
    if (guessedLength < expectedLength) {
      guessedLength = expectedLength;
    }
  }
  return guessedLength;
}

void VGMItem::SetGuessedLength() {
  for (const auto child : m_children) {
    child->SetGuessedLength();
  }
  if (unLength == 0) {
    unLength = GuessLength();
  }
}

//  ****************
//  VGMContainerItem
//  ****************
//
// VGMContainerItem::VGMContainerItem() : VGMItem() {
//   AddContainer(headers);
//   AddContainer(localitems);
// }
//
// VGMContainerItem::VGMContainerItem(
//     VGMFile *vgmfile, uint32_t offset, uint32_t length, std::string name, EventColor color)
//     : VGMItem(vgmfile, offset, length, std::move(name), color) {
//   AddContainer(headers);
//   AddContainer(localitems);
// }
//
// VGMContainerItem::~VGMContainerItem() {
//   DeleteVect(headers);
//   DeleteVect(localitems);
// }
//
// // Get the bottom-most VGMItem at a given offset. If includeContainer is false, only return
// // a non-VGMContainerItem. Otherwise, return the bottom-most VGMContainerItem if a
// // non-VGMContainerItem does not exist at the offset.
// VGMItem *VGMContainerItem::GetItemFromOffset(uint32_t offset, bool includeContainer, bool matchStartOffset) {
//   for (const auto *container : containers) {
//     for (VGMItem *item : *container) {
//       if (item->unLength == 0 || (offset >= item->dwOffset && offset < item->dwOffset + item->unLength)) {
//         if (VGMItem *foundItem = item->GetItemFromOffset(offset, includeContainer, matchStartOffset))
//           return foundItem;
//       }
//     }
//   }
//
//   if (includeContainer && (matchStartOffset ? offset == dwOffset : offset >= dwOffset) &&
//       (offset < dwOffset + unLength)) {
//     return this;
//   } else {
//     return nullptr;
//   }
// }
//
// // Guess length of a container from its descendants
// uint32_t VGMContainerItem::GuessLength() {
//   uint32_t guessedLength = 0;
//
//   // NOTE: children items can sometimes overlap each other
//   for (const auto *container : containers) {
//     for (VGMItem *item : *container) {
//       assert(dwOffset <= item->dwOffset);
//
//       uint32_t itemLength = item->unLength;
//       if (unLength == 0) {
//         itemLength = item->GuessLength();
//       }
//
//       uint32_t expectedLength = item->dwOffset + itemLength - dwOffset;
//       if (guessedLength < expectedLength) {
//         guessedLength = expectedLength;
//       }
//     }
//   }
//
//   return guessedLength;
// }
//
// void VGMContainerItem::SetGuessedLength() {
//   for (const auto *container : containers) {
//     for (VGMItem *item : *container) {
//       item->SetGuessedLength();
//     }
//   }
//
//   if (unLength == 0) {
//     unLength = GuessLength();
//   }
// }
//
// void VGMContainerItem::AddToUI(VGMItem *parent, void *UI_specific) {
//   VGMItem::AddToUI(parent, UI_specific);
//   for (const auto *container : containers) {
//     for (VGMItem *item : *container) {
//       item->AddToUI(this, UI_specific);
//     }
//   }
// }
//
// VGMHeader *VGMContainerItem::addHeader(uint32_t offset, uint32_t length, const std::string &name) {
//   auto *header = new VGMHeader(this, offset, length, name);
//   headers.push_back(header);
//   return header;
// }
//
// void VGMContainerItem::AddItem(VGMItem *item) {
//   localitems.push_back(item);
// }
//
// void VGMContainerItem::AddSimpleItem(uint32_t offset, uint32_t length, const std::string &name) {
//   localitems.push_back(new VGMItem(vgmFile(), offset, length, name, CLR_HEADER));
// }
//
// void VGMContainerItem::addUnknownChild(uint32_t offset, uint32_t length) {
//   localitems.push_back(new VGMItem(vgmFile(), offset, length, "Unknown"));
// }
