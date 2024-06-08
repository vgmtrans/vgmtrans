/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "VGMItem.h"
#include "RawFile.h"

class VGMColl;
class Format;

class VGMFile : public VGMItem {
public:
  VGMFile(std::string format, RawFile *theRawFile, uint32_t offset, uint32_t length = 0,
          std::string name = "VGM File");
  ~VGMFile() override = default;

  void addToUI(VGMItem *parent, void *UI_specific) override;

  [[nodiscard]] std::string description() override;

  virtual bool loadVGMFile() = 0;
  virtual bool load() = 0;
  Format* format() const;
  [[nodiscard]] std::string formatName();

  virtual uint32_t id() const { return m_id; }
  void setId(uint32_t newId) { m_id = newId; }

  void addCollAssoc(VGMColl *coll);
  void removeCollAssoc(VGMColl *coll);
  [[nodiscard]] RawFile *rawFile() const;

  [[nodiscard]] size_t size() const noexcept { return unLength; }

  uint32_t readBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) const;

  inline uint8_t readByte(uint32_t offset) const { return m_rawfile->readByte(offset); }
  inline uint16_t readShort(uint32_t offset) const { return m_rawfile->readShort(offset); }
  inline uint32_t readWord(uint32_t offset) const { return m_rawfile->readWord(offset); }
  inline uint16_t readShortBE(uint32_t offset) const { return m_rawfile->readShortBE(offset); }
  inline uint32_t readWordBE(uint32_t offset) const { return m_rawfile->readWordBE(offset); }
  inline bool isValidOffset(uint32_t offset) const { return m_rawfile->isValidOffset(offset); }

  uint32_t startOffset() const { return dwOffset; }
  /*
   * For whatever reason, you can create null-length VGMItems.
   * The only safe way for now is to
   * assume maximum length
   */
  uint32_t endOffset() const { return static_cast<uint32_t>(m_rawfile->size()); }

  [[nodiscard]] const char *data() const { return m_rawfile->data() + dwOffset; }

  std::vector<VGMColl*> assocColls;

private:
  RawFile* m_rawfile;
  std::string m_format;
  uint32_t m_id;
};

// *********
// VGMHeader
// *********

class VGMHeader : public VGMItem {
public:
  VGMHeader(const VGMItem *parItem, uint32_t offset = 0, uint32_t length = 0,
            const std::string &name = "Header");
  ~VGMHeader() override;

  Icon icon() override { return ICON_BINARY; };

  void addPointer(uint32_t offset, uint32_t length, uint32_t destAddress, bool notNull,
                  const std::string &name = "Pointer");
  void addTempo(uint32_t offset, uint32_t length, const std::string &name = "Tempo");
  void addSig(uint32_t offset, uint32_t length, const std::string &name = "Signature");
};

// *************
// VGMHeaderItem
// *************

class VGMHeaderItem : public VGMItem {
public:
  enum HdrItemType {
    HIT_POINTER,
    HIT_TEMPO,
    HIT_SIG,
    HIT_GENERIC,
    HIT_UNKNOWN
  };  // HIT = Header Item Type

  VGMHeaderItem(const VGMHeader *hdr, HdrItemType theType, uint32_t offset, uint32_t length,
                const std::string &name);
  Icon icon() override;

  HdrItemType type;
};
