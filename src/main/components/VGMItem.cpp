#include "base/Types.h"
#include "VGMItem.h"
#include "LogManager.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "Root.h"
#include "Helper.h"

VGMItem::VGMItem() : type(Type::Unknown), m_vgmfile(nullptr), m_offset(0), m_length(0) {
}

VGMItem::VGMItem(VGMFile *vgmfile, u32 offset, u32 length, std::string name, Type type)
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

bool VGMItem::isItemAtOffset(u32 offset, bool matchStartOffset) {
  return getItemAtOffset(offset, matchStartOffset) != nullptr;
}

VGMItem* VGMItem::getItemAtOffset(u32 offset, bool matchStartOffset) {
  if (m_children.empty()) {
    if ((matchStartOffset ? offset == m_offset : offset >= m_offset) && (offset < m_offset + m_length)) {
      return this;
    }
  }
  if (m_children.size() == 1) {
    return m_children[0]->getItemAtOffset(offset, matchStartOffset);
  }

  ensureChildrenSorted();
  // Binary search by start offset, then scan backwards only across potentially overlapping items.
  const auto begin = m_children.begin();
  const auto end = m_children.end();
  auto it = std::upper_bound(begin, end, offset,
      [](u32 off, const VGMItem* child) { return off < child->offset(); });

  for (auto idx = static_cast<size_t>(it - begin); idx > 0; --idx) {
    VGMItem* child = m_children[idx - 1];
    if (VGMItem* foundItem = child->getItemAtOffset(offset, matchStartOffset)) {
      return foundItem;
    }
    if (!m_childrenPrefixMaxEnd.empty() &&
        m_childrenPrefixMaxEnd[idx - 1] <= static_cast<u64>(offset)) {
      break;
    }
  }

  return nullptr;
}

void VGMItem::setOffset(u32 offset) {
  if (m_offset == offset) {
    return;
  }
  m_offset = offset;
  invalidateParentCache(true);
}

void VGMItem::setLength(u32 length) {
  if (m_length == length) {
    return;
  }
  m_length = length;
  invalidateParentCache(false);
}

void VGMItem::setRange(u32 offset, u32 length) {
  const bool offsetChanged = (m_offset != offset);
  const bool lengthChanged = (m_length != length);
  if (!offsetChanged && !lengthChanged) {
    return;
  }
  m_offset = offset;
  m_length = length;
  invalidateParentCache(offsetChanged);
}

void VGMItem::addToUI(VGMItem *parent, void *UI_specific) {
  pRoot->UI_addItem(this, parent, name(), UI_specific);

  for (const auto child : m_children) {
    child->addToUI(this, UI_specific);
  }
}

u32 VGMItem::readBytes(u32 nIndex, u32 nCount, void *pBuffer) const {
  return m_vgmfile->readBytes(nIndex, nCount, pBuffer);
}

u8 VGMItem::readByte(u32 offset) const {
  return m_vgmfile->readByte(offset);
}

u16 VGMItem::readShort(u32 offset) const {
  return m_vgmfile->readShort(offset);
}

u32 VGMItem::getWord(u32 offset) const {
  return m_vgmfile->readWord(offset);
}

// GetShort Big Endian
u16 VGMItem::getShortBE(u32 offset) const {
  return m_vgmfile->readShortBE(offset);
}

// GetWord Big Endian
u32 VGMItem::getWordBE(u32 offset) const {
  return m_vgmfile->readWordBE(offset);
}

bool VGMItem::isValidOffset(u32 offset) const {
  return m_vgmfile->isValidOffset(offset);
}

VGMItem* VGMItem::addChild(VGMItem *item) {
  item->m_vgmfile = vgmFile();
  item->m_parent = this;
  m_children.emplace_back(item);
  m_childrenSorted = false;
  m_childrenPrefixMaxEnd.clear();
  return item;
}

VGMItem* VGMItem::addChild(u32 offset, u32 length, const std::string &name) {
  auto child = new VGMItem(vgmFile(), offset, length, name, Type::Header);
  child->m_parent = this;
  m_children.emplace_back(child);
  m_childrenSorted = false;
  m_childrenPrefixMaxEnd.clear();
  return child;
}

VGMItem* VGMItem::addUnknownChild(u32 offset, u32 length) {
  auto child = new VGMItem(vgmFile(), offset, length, "Unknown");
  child->m_parent = this;
  m_children.emplace_back(child);
  m_childrenSorted = false;
  m_childrenPrefixMaxEnd.clear();
  return child;
}

VGMHeader* VGMItem::addHeader(u32 offset, u32 length, const std::string &name) {
  auto *header = new VGMHeader(this, offset, length, name);
  header->m_parent = this;
  m_children.emplace_back(header);
  m_childrenSorted = false;
  m_childrenPrefixMaxEnd.clear();
  return header;
}

void VGMItem::removeChildren() {
  for (auto *child : m_children) {
    child->m_parent = nullptr;
  }
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
  // Lazy path: only sort/rebuild when a lookup needs it (single-child handled in caller).
  if (!m_childrenSorted) {
    std::sort(m_children.begin(), m_children.end(),
              [](const VGMItem* a, const VGMItem* b) { return a->offset() < b->offset(); });
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
  u64 maxEnd = 0;
  for (const auto* child : m_children) {
    assert(child->length() != 0);
    if (child->length() == 0) {
      L_WARN("getItemAtOffset() called on an item with a child of length 0 - could screw up prefix max cache. "
             "Item: {}. Child: {} at offset: {:X}", name(), child->name(), child->offset());
    }
    const u32 span = (child->length() != 0) ? child->length() : child->guessLength();
    const u64 end = static_cast<u64>(child->offset()) + span;
    maxEnd = std::max(maxEnd, end);
    m_childrenPrefixMaxEnd.push_back(maxEnd);
  }
}

void VGMItem::invalidateParentCache(bool offsetChanged) {
  if (!m_parent) {
    return;
  }
  if (offsetChanged) {
    m_parent->m_childrenSorted = false;
  }
  m_parent->m_childrenPrefixMaxEnd.clear();
}

void VGMItem::sortChildrenByOffset() {
  std::ranges::sort(m_children, [](const VGMItem *a, const VGMItem *b) {
    return a->offset() < b->offset();
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
u32 VGMItem::guessLength() const {
  if (m_children.empty()) {
    return m_length;
  }
  // NOTE: child items can sometimes overlap each other
  u32 guessedLength = 0;
  for (const auto child : m_children) {
    assert(m_offset <= child->offset());

    u32 itemLength = child->length();
    if (m_length == 0) {
      itemLength = child->guessLength();
    }

    u32 expectedLength = child->offset() + itemLength - m_offset;
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
