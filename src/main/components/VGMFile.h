/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "util/types.h"

#include "VGMItem.h"
#include "RawFile.h"

class VGMColl;
class Format;

class VGMFile : public VGMItem {
public:
  VGMFile(std::string format, RawFile *theRawFile, u32 offset, u32 length = 0,
          std::string name = "VGM File");
  ~VGMFile() override = default;

  void addToUI(VGMItem *parent, void *UI_specific) override;

  [[nodiscard]] std::string description() override;

  virtual bool loadVGMFile(bool useMatcher) = 0;
  virtual bool load() = 0;
  Format* format() const;
  [[nodiscard]] std::string formatName();

  virtual u32 id() const { return m_id; }
  void setId(u32 newId) { m_id = newId; }

  void addCollAssoc(VGMColl *coll);
  void removeCollAssoc(VGMColl *coll);
  [[nodiscard]] RawFile *rawFile() const;

  [[nodiscard]] size_t size() const noexcept { return length(); }

  u32 readBytes(u32 nIndex, u32 nCount, void *pBuffer) const;

  inline u8 readByte(u32 offset) const { return m_rawfile->readByte(offset); }
  inline u16 readShort(u32 offset) const { return m_rawfile->readShort(offset); }
  inline u32 readWord(u32 offset) const { return m_rawfile->readWord(offset); }
  inline u16 readShortBE(u32 offset) const { return m_rawfile->readShortBE(offset); }
  inline u32 readWordBE(u32 offset) const { return m_rawfile->readWordBE(offset); }
  inline bool isValidOffset(u32 offset) const { return m_rawfile->isValidOffset(offset); }

  u32 startOffset() const { return offset(); }
  /*
   * For whatever reason, you can create null-length VGMItems.
   * The only safe way for now is to
   * assume maximum length
   */
  u32 endOffset() const { return static_cast<u32>(m_rawfile->size()); }

  [[nodiscard]] const char *data() const { return m_rawfile->data() + offset(); }

  std::vector<VGMColl*> assocColls;

private:
  RawFile* m_rawfile;
  std::string m_format;
  u32 m_id;
};

// *********
// VGMHeader
// *********

class VGMHeader : public VGMItem {
public:
  VGMHeader(const VGMItem *parItem, u32 offset = 0, u32 length = 0,
            const std::string &name = "Header");
  ~VGMHeader() override;

  void addPointer(u32 offset, u32 length, u32 destAddress, bool notNull,
                  const std::string &name = "Pointer");
  void addTempo(u32 offset, u32 length, const std::string &name = "Tempo");
  void addSig(u32 offset, u32 length, const std::string &name = "Signature");
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

  VGMHeaderItem(const VGMHeader *hdr, HdrItemType headerType, u32 offset, u32 length,
                const std::string &name);

private:
  static Type resolveType(HdrItemType headerType) {
    switch (headerType) {
      case HIT_UNKNOWN: return Type::Unknown;
      case HIT_POINTER: return Type::Misc;
      case HIT_TEMPO:   return Type::Tempo;
      case HIT_SIG:     return Type::Misc;
      default:          return Type::Misc;
    }
  }
};
