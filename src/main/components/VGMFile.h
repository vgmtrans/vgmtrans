/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "VGMItem.h"
#include "RawFile.h"
#include "Menu.h"

enum FmtID : unsigned int;
class VGMColl;
class Format;

enum FileType {
    FILETYPE_UNDEFINED,
    FILETYPE_SEQ,
    FILETYPE_INSTRSET,
    FILETYPE_SAMPCOLL,
    FILETYPE_MISC
};

class VGMFile : public VGMContainerItem {
   public:
    BEGIN_MENU(VGMFile)
    MENU_ITEM(VGMFile, OnClose, "Close")
    MENU_ITEM(VGMFile, OnSaveAsRaw, "Dump raw format")
    END_MENU()

   public:
    VGMFile(FileType fileType, /*FmtID fmtID,*/
            const std::string &format, RawFile *theRawFile, uint32_t offset, uint32_t length = 0,
            std::string theName = "VGM File");
    virtual ~VGMFile();

    virtual ItemType GetType() const { return ITEMTYPE_VGMFILE; }
    FileType GetFileType() { return file_type; }

    virtual void AddToUI(VGMItem *parent, void *UI_specific);

    const std::string *GetName(void) const;

    bool OnClose();
    bool OnSaveAsRaw();
    bool OnSaveAllAsRaw();

    bool LoadVGMFile();
    virtual bool Load() = 0;
    Format *GetFormat();
    const std::string &GetFormatName();

    virtual uint32_t GetID() { return id; }

    void AddCollAssoc(VGMColl *coll);
    void RemoveCollAssoc(VGMColl *coll);
    RawFile *GetRawFile();

    size_t size() const noexcept { return unLength; }
    std::string name() const noexcept { return m_name; }

    uint32_t GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer);

    inline uint8_t GetByte(uint32_t offset) const { return rawfile->GetByte(offset); }
    inline uint16_t GetShort(uint32_t offset) const { return rawfile->GetShort(offset); }
    inline uint32_t GetWord(uint32_t offset) const { return rawfile->GetWord(offset); }
    inline uint16_t GetShortBE(uint32_t offset) const { return rawfile->GetShortBE(offset); }
    inline uint32_t GetWordBE(uint32_t offset) const { return rawfile->GetWordBE(offset); }
    inline bool IsValidOffset(uint32_t offset) const { return rawfile->IsValidOffset(offset); }

    size_t GetStartOffset() { return dwOffset; }
    /*
     * For whatever reason, you can create null-length VGMItems.
     * The only safe way for now is to
     * assume maximum length
     */
    size_t GetEndOffset() { return rawfile->size(); }

    const char *data() const { return rawfile->data() + dwOffset; }

    RawFile *rawfile;
    std::list<VGMColl *> assocColls;

   protected:
    FileType file_type;
    const std::string &format;
    uint32_t id;
    std::string m_name;
};

// *********
// VGMHeader
// *********

class VGMHeader : public VGMContainerItem {
   public:
    VGMHeader(VGMItem *parItem, uint32_t offset = 0, uint32_t length = 0,
              const std::string &name = "Header");
    virtual ~VGMHeader();

    virtual Icon GetIcon() { return ICON_BINARY; };

    void AddPointer(uint32_t offset, uint32_t length, uint32_t destAddress, bool notNull,
                    const std::string &name = "Pointer");
    void AddTempo(uint32_t offset, uint32_t length, const std::string &name = "Tempo");
    void AddSig(uint32_t offset, uint32_t length, const std::string &name = "Signature");

    // vector<VGMItem*> items;
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

    VGMHeaderItem(VGMHeader *hdr, HdrItemType theType, uint32_t offset, uint32_t length,
                  const std::string &name);
    virtual Icon GetIcon();

   public:
    HdrItemType type;
};
