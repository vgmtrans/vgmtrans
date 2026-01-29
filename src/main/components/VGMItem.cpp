#include "VGMItem.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "Root.h"
#include "helper.h"

VGMItem::VGMItem() : type(Type::Unknown), m_vgmfile(nullptr), m_offset(0), m_length(0) {
}

VGMItem::VGMItem(VGMFile *vgmfile, uint32_t offset, uint32_t length, std::string name, Type type)
    : type(type),
      m_vgmfile(vgmfile),
      m_offset(offset),
      m_length(length),
      m_name(std::move(name)) {
}

VGMItem::~VGMItem() {
  deleteVect(m_children);
}

bool operator>(const VGMItem &item1, const VGMItem &item2) {
  return item1.offset() > item2.offset();
}

bool operator<=(const VGMItem &item1, const VGMItem &item2) {
  return item1.offset() <= item2.offset();
}

bool operator<(const VGMItem &item1, const VGMItem &item2) {
  return item1.offset() < item2.offset();
}

bool operator>=(const VGMItem &item1, const VGMItem &item2) {
  return item1.offset() >= item2.offset();
}

RawFile *VGMItem::rawFile() const {
  return m_vgmfile->rawFile();
}

bool VGMItem::isItemAtOffset(uint32_t offset, bool matchStartOffset) {
  return getItemAtOffset(offset, matchStartOffset) != nullptr;
}

VGMItem* VGMItem::getItemAtOffset(uint32_t offset, bool matchStartOffset) {
  if (m_children.empty()) {
    if ((matchStartOffset ? offset == m_offset : offset >= m_offset) && (offset < m_offset + m_length)) {
      return this;
    }
  }

  for (const auto child : m_children) {
    if (VGMItem *foundItem = child->getItemAtOffset(offset, matchStartOffset))
      return foundItem;
  }

  return nullptr;
}

void VGMItem::setOffset(uint32_t offset) {
  if (m_offset == offset) {
    return;
  }
  m_offset = offset;
}

void VGMItem::setLength(uint32_t length) {
  if (m_length == length) {
    return;
  }
  m_length = length;
}

void VGMItem::setRange(uint32_t offset, uint32_t length) {
  const bool offsetChanged = (m_offset != offset);
  const bool lengthChanged = (m_length != length);
  if (!offsetChanged && !lengthChanged) {
    return;
  }
  m_offset = offset;
  m_length = length;
}

void VGMItem::addToUI(VGMItem *parent, void *UI_specific) {
  pRoot->UI_addItem(this, parent, name(), UI_specific);

  for (const auto child : m_children) {
    child->addToUI(this, UI_specific);
  }
}

uint32_t VGMItem::readBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) const {
  return m_vgmfile->readBytes(nIndex, nCount, pBuffer);
}

uint8_t VGMItem::readByte(uint32_t offset) const {
  return m_vgmfile->readByte(offset);
}

uint16_t VGMItem::readShort(uint32_t offset) const {
  return m_vgmfile->readShort(offset);
}

uint32_t VGMItem::getWord(uint32_t offset) const {
  return m_vgmfile->readWord(offset);
}

// GetShort Big Endian
uint16_t VGMItem::getShortBE(uint32_t offset) const {
  return m_vgmfile->readShortBE(offset);
}

// GetWord Big Endian
uint32_t VGMItem::getWordBE(uint32_t offset) const {
  return m_vgmfile->readWordBE(offset);
}

bool VGMItem::isValidOffset(uint32_t offset) const {
  return m_vgmfile->isValidOffset(offset);
}

VGMItem* VGMItem::addChild(VGMItem *item) {
  item->m_vgmfile = vgmFile();
  item->m_parent = this;
  m_children.emplace_back(item);
  return item;
}

VGMItem* VGMItem::addChild(uint32_t offset, uint32_t length, const std::string &name) {
  auto child = new VGMItem(vgmFile(), offset, length, name, Type::Header);
  child->m_parent = this;
  m_children.emplace_back(child);
  return child;
}

VGMItem* VGMItem::addUnknownChild(uint32_t offset, uint32_t length) {
  auto child = new VGMItem(vgmFile(), offset, length, "Unknown");
  child->m_parent = this;
  m_children.emplace_back(child);
  return child;
}

VGMHeader* VGMItem::addHeader(uint32_t offset, uint32_t length, const std::string &name) {
  auto *header = new VGMHeader(this, offset, length, name);
  header->m_parent = this;
  m_children.emplace_back(header);
  return header;
}

void VGMItem::removeChildren() {
  for (auto *child : m_children) {
    child->m_parent = nullptr;
  }
  m_children.clear();
}

void VGMItem::transferChildren(VGMItem* destination) {
  destination->addChildren(m_children);
  m_children.clear();
}

void VGMItem::sortChildrenByOffset() {
  std::ranges::sort(m_children, [](const VGMItem *a, const VGMItem *b) {
    return a->offset() < b->offset();
  });

  // Recursively sort the children of each child, if they have any children
  for (VGMItem* child : m_children) {
    if (!child->children().empty()) {
      child->sortChildrenByOffset();
    }
  }
}

// Guess length of a container from its descendants
uint32_t VGMItem::guessLength() {
  if (m_children.empty()) {
    return m_length;
  }
  // NOTE: child items can sometimes overlap each other
  uint32_t guessedLength = 0;
  for (const auto child : m_children) {
    assert(m_offset <= child->offset());

    uint32_t itemLength = child->length();
    if (m_length == 0) {
      itemLength = child->guessLength();
    }

    uint32_t expectedLength = child->offset() + itemLength - m_offset;
    if (guessedLength < expectedLength) {
      guessedLength = expectedLength;
    }
  }
  return guessedLength;
}

void VGMItem::setGuessedLength() {
  for (const auto child : m_children) {
    child->setGuessedLength();
  }
  if (m_length == 0) {
    setLength(guessLength());
  }
}
