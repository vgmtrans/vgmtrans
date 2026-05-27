/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "base/Types.h"
#include "Root.h"
#include "VGMFile.h"
#include "Format.h"

VGMFile::VGMFile(std::string fmt, RawFile *theRawFile, u32 offset,
                 u32 length, std::string name)
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
  return Format::formatFromName(m_format);
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

u32 VGMFile::readBytes(u32 nIndex, u32 nCount, void *pBuffer) const {
  // if length() != 0, verify that we're within the bounds of the file, and truncate num read
  // bytes to end of file
  if (length() != 0) {
    u32 endOff = offset() + length();
    assert(nIndex >= offset() && nIndex < endOff);
    if (nIndex + nCount > endOff)
      nCount = endOff - nIndex;
  }

  return m_rawfile->readBytes(nIndex, nCount, pBuffer);
}

// *********
// VGMHeader
// *********

VGMHeader::VGMHeader(const VGMItem *parItem, u32 offset, u32 length, const std::string &name)
    : VGMItem(parItem->vgmFile(), offset, length, name) {}

VGMHeader::~VGMHeader() = default;

void VGMHeader::addPointer(u32 offset, u32 length, u32 /*destAddress*/, bool /*notNull*/,
                           const std::string &name) {
  addChild(new VGMHeaderItem(this, VGMHeaderItem::HIT_POINTER, offset, length, name));
}

void VGMHeader::addTempo(u32 offset, u32 length, const std::string &name) {
  addChild(new VGMHeaderItem(this, VGMHeaderItem::HIT_TEMPO, offset, length, name));
}

void VGMHeader::addSig(u32 offset, u32 length, const std::string &name) {
  addChild(new VGMHeaderItem(this, VGMHeaderItem::HIT_SIG, offset, length, name));
}

// *************
// VGMHeaderItem
// *************

VGMHeaderItem::VGMHeaderItem(const VGMHeader *hdr, HdrItemType headerType, u32 offset, u32 length,
                             const std::string &name)
    : VGMItem(hdr->vgmFile(), offset, length, name, resolveType(headerType)) {
}
