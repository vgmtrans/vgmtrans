#include "VGMItem.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "Root.h"
#include "helper.h"

VGMItem::VGMItem() : m_vgmfile(nullptr), dwOffset(0), unLength(0), type(Type::Unknown) {
}

VGMItem::VGMItem(VGMFile *vgmfile, uint32_t offset, uint32_t length, std::string name, Type type)
    : m_vgmfile(vgmfile), m_name(std::move(name)), dwOffset(offset), unLength(length), type(type) {
}

VGMItem::~VGMItem() {
  deleteVect(m_children);
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

bool VGMItem::isItemAtOffset(uint32_t offset, bool matchStartOffset) {
  return getItemAtOffset(offset, matchStartOffset) != nullptr;
}

VGMItem* VGMItem::getItemAtOffset(uint32_t offset, bool matchStartOffset) {
  if (m_children.empty()) {
    if ((matchStartOffset ? offset == dwOffset : offset >= dwOffset) && (offset < dwOffset + unLength)) {
      return this;
    }
  }

  ensureChildrenSorted();
  // Binary search by start offset, then scan backwards only across potentially overlapping items.
  const auto begin = m_children.begin();
  const auto end = m_children.end();
  auto it = std::upper_bound(begin, end, offset,
      [](uint32_t off, const VGMItem* child) { return off < child->dwOffset; });

  for (auto idx = static_cast<size_t>(it - begin); idx > 0; --idx) {
    VGMItem* child = m_children[idx - 1];
    if (VGMItem* foundItem = child->getItemAtOffset(offset, matchStartOffset)) {
      return foundItem;
    }
    if (!m_childrenPrefixMaxEnd.empty() &&
        m_childrenPrefixMaxEnd[idx - 1] <= static_cast<uint64_t>(offset)) {
      break;
    }
  }

  return nullptr;
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
  m_children.emplace_back(item);
  m_childrenSorted = false;
  m_childrenPrefixMaxEnd.clear();
  return item;
}

VGMItem* VGMItem::addChild(uint32_t offset, uint32_t length, const std::string &name) {
  auto child = new VGMItem(vgmFile(), offset, length, name, Type::Header);
  m_children.emplace_back(child);
  m_childrenSorted = false;
  m_childrenPrefixMaxEnd.clear();
  return child;
}

VGMItem* VGMItem::addUnknownChild(uint32_t offset, uint32_t length) {
  auto child = new VGMItem(vgmFile(), offset, length, "Unknown");
  m_children.emplace_back(child);
  m_childrenSorted = false;
  m_childrenPrefixMaxEnd.clear();
  return child;
}

VGMHeader* VGMItem::addHeader(uint32_t offset, uint32_t length, const std::string &name) {
  auto *header = new VGMHeader(this, offset, length, name);
  m_children.emplace_back(header);
  m_childrenSorted = false;
  m_childrenPrefixMaxEnd.clear();
  return header;
}

void VGMItem::removeChildren() {
  m_children.clear();
  m_childrenSorted = true;
  m_childrenPrefixMaxEnd.clear();
}

void VGMItem::transferChildren(VGMItem* destination) {
  destination->addChildren(m_children);
  m_children.clear();
  m_childrenSorted = true;
  m_childrenPrefixMaxEnd.clear();
}

void VGMItem::ensureChildrenSorted() {
  // Lazy path: only sort/rebuild when a lookup needs it.
  if (m_children.size() < 2) {
    m_childrenSorted = true;
  }
  if (!m_childrenSorted) {
    std::sort(m_children.begin(), m_children.end(),
              [](const VGMItem* a, const VGMItem* b) { return a->dwOffset < b->dwOffset; });
    m_childrenSorted = true;
    rebuildChildPrefixMaxEnd();
    return;
  }
  if (m_childrenPrefixMaxEnd.size() != m_children.size()) {
    rebuildChildPrefixMaxEnd();
  }
}

void VGMItem::rebuildChildPrefixMaxEnd() {
  m_childrenPrefixMaxEnd.clear();
  m_childrenPrefixMaxEnd.reserve(m_children.size());
  uint64_t maxEnd = 0;
  for (const auto* child : m_children) {
    const uint64_t end = static_cast<uint64_t>(child->dwOffset) + child->unLength;
    maxEnd = std::max(maxEnd, end);
    m_childrenPrefixMaxEnd.push_back(maxEnd);
  }
}

void VGMItem::sortChildrenByOffset() {
  std::ranges::sort(m_children, [](const VGMItem *a, const VGMItem *b) {
    return a->dwOffset < b->dwOffset;
  });
  m_childrenSorted = true;
  rebuildChildPrefixMaxEnd();

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
    return unLength;
  }
  // NOTE: child items can sometimes overlap each other
  uint32_t guessedLength = 0;
  for (const auto child : m_children) {
    assert(dwOffset <= child->dwOffset);

    uint32_t itemLength = child->unLength;
    if (unLength == 0) {
      itemLength = child->guessLength();
    }

    uint32_t expectedLength = child->dwOffset + itemLength - dwOffset;
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
  if (unLength == 0) {
    unLength = guessLength();
  }
}
