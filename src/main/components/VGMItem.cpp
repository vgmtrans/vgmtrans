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

VGMItem::~VGMItem() {
  DeleteVect(m_children);
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
  if (m_children.empty()) {
    if ((matchStartOffset ? offset == dwOffset : offset >= dwOffset) && (offset < dwOffset + unLength)) {
      return this;
    }
  }

  for (const auto child : m_children) {
    if (VGMItem *foundItem = child->GetItemFromOffset(offset, matchStartOffset))
      return foundItem;
  }

  return nullptr;
}

void VGMItem::AddToUI(VGMItem *parent, void *UI_specific) {
  pRoot->UI_AddItem(this, parent, name(), UI_specific);

  for (const auto child : m_children) {
    child->AddToUI(this, UI_specific);
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

VGMItem* VGMItem::addChild(uint32_t offset, uint32_t length, const std::string &name) {
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

void VGMItem::sortChildrenByOffset() {
  std::ranges::sort(m_children, [](const VGMItem *a, const VGMItem *b) {
    return a->dwOffset < b->dwOffset;
  });

  // Recursively sort the children of each child, if they have any children
  for (VGMItem* child : m_children) {
    if (!child->children().empty()) {
      child->sortChildrenByOffset();
    }
  }
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
