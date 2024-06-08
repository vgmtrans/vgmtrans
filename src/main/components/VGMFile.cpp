/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Root.h"
#include "VGMFile.h"
#include "Format.h"

VGMFile::VGMFile(std::string fmt, RawFile *theRawFile, uint32_t offset,
                 uint32_t length, std::string name)
    : VGMItem(this, offset, length, std::move(name)),
      m_rawfile(theRawFile),
      m_format(std::move(fmt)),
      m_id(-1) {}

// Only difference between this AddToUI and VGMItemContainer's version is that we do not add
// this as an item because we do not want the VGMFile to be itself an item in the Item View
void VGMFile::addToUI(VGMItem* /*parent*/, void* UI_specific) {
  for (const auto child : children()) {
    child->addToUI(this, UI_specific);
  }
}

Format *VGMFile::format() const {
  return Format::getFormatFromName(m_format);
}

std::string VGMFile::formatName() {
  return m_format;
}

std::string VGMFile::description() {
  auto filename = this->m_rawfile->name();
  auto formatName = this->format()->getName();
  return "Format: " + formatName + "     Source File: \"" + filename + "\"";
}

void VGMFile::addCollAssoc(VGMColl *coll) {
  assocColls.push_back(coll);
}

void VGMFile::removeCollAssoc(VGMColl *coll) {
  auto iter = std::ranges::find(assocColls, coll);
  if (iter != assocColls.end())
    assocColls.erase(iter);
}

// These functions are common to all VGMItems, but no reason to refer to vgmfile
// or call rawFile() if the item itself is a VGMFile
RawFile *VGMFile::rawFile() const {
  return m_rawfile;
}

uint32_t VGMFile::readBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) const {
  // if unLength != 0, verify that we're within the bounds of the file, and truncate num read
  // bytes to end of file
  if (unLength != 0) {
    uint32_t endOff = dwOffset + unLength;
    assert(nIndex >= dwOffset && nIndex < endOff);
    if (nIndex + nCount > endOff)
      nCount = endOff - nIndex;
  }

  return m_rawfile->readBytes(nIndex, nCount, pBuffer);
}

// *********
// VGMHeader
// *********

VGMHeader::VGMHeader(const VGMItem *parItem, uint32_t offset, uint32_t length, const std::string &name)
    : VGMItem(parItem->vgmFile(), offset, length, name) {}

VGMHeader::~VGMHeader() = default;

void VGMHeader::addPointer(uint32_t offset, uint32_t length, uint32_t /*destAddress*/, bool /*notNull*/,
                           const std::string &name) {
  addChild(new VGMHeaderItem(this, VGMHeaderItem::HIT_POINTER, offset, length, name));
}

void VGMHeader::addTempo(uint32_t offset, uint32_t length, const std::string &name) {
  addChild(new VGMHeaderItem(this, VGMHeaderItem::HIT_TEMPO, offset, length, name));
}

void VGMHeader::addSig(uint32_t offset, uint32_t length, const std::string &name) {
  addChild(new VGMHeaderItem(this, VGMHeaderItem::HIT_SIG, offset, length, name));
}

// *************
// VGMHeaderItem
// *************

VGMHeaderItem::VGMHeaderItem(const VGMHeader *hdr, HdrItemType theType, uint32_t offset, uint32_t length,
                             const std::string &name)
    : VGMItem(hdr->vgmFile(), offset, length, name, CLR_HEADER), type(theType) {}

VGMItem::Icon VGMHeaderItem::icon() {
  switch (type) {
    case HIT_UNKNOWN:
      return ICON_UNKNOWN;
    case HIT_POINTER:
      return ICON_BINARY;
    case HIT_TEMPO:
      return ICON_TEMPO;
    case HIT_SIG:
      [[fallthrough]];
    default:
      return ICON_BINARY;
  }
}