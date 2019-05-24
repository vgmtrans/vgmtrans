/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <fstream>

#include "common.h"
#include "DataSeg.h"
#include "BytePattern.h"
#include "VGMTag.h"
#include "DataBlock.h"

class VGMFile;
class VGMItem;

enum ProcessFlag { PF_USELOADERS = 1, PF_USESCANNERS = 2 };

class RawFile {
   public:
    RawFile(void);
    RawFile(const std::wstring name, uint32_t fileSize = 0, bool bCanRead = true,
            const VGMTag tag = VGMTag());
    virtual ~RawFile(void);

    bool open(const std::wstring &filename);
    void close();
    unsigned long size(void);
    inline const wchar_t *GetFullPath() { return fullpath.c_str(); }
    inline const wchar_t *GetFileName() {
        return filename.c_str();
    }  // returns the filename with extension
    inline const std::wstring &GetExtension() { return extension; }
    inline const std::wstring &GetParRawFileFullPath() { return parRawFileFullPath; }
    static std::wstring getFileNameFromPath(const std::wstring &s);
    static std::wstring getExtFromPath(const std::wstring &s);
    static std::wstring removeExtFromPath(const std::wstring &s);
    VGMItem *GetItemFromOffset(long offset);
    VGMFile *GetVGMFileFromOffset(long offset);

    virtual int FileRead(void *dest, uint32_t index, uint32_t length);

    void UpdateBuffer(uint32_t index);

    float GetProPreRatio(void) { return propreRatio; }
    void SetProPreRatio(float newRatio);

    uint8_t *rawData() noexcept { return m_data->data(); }
    DataBlock *data() noexcept { return m_data.get(); }

    inline uint8_t &operator[](uint32_t offset) {
        if ((offset < m_data->beginOffset()) || (offset >= m_data->endOffset()))
            UpdateBuffer(offset);
        return (*m_data)[offset];
    }

    inline uint8_t GetByte(uint32_t nIndex) {
        if ((nIndex < m_data->beginOffset()) || (nIndex + 1 > m_data->endOffset()))
            UpdateBuffer(nIndex);
        return (*m_data)[nIndex];
    }

    inline uint16_t GetShort(uint32_t nIndex) {
        if ((nIndex < m_data->beginOffset()) || (nIndex + 2 > m_data->endOffset()))
            UpdateBuffer(nIndex);
        return m_data->GetShort(nIndex);
    }

    inline uint32_t GetWord(uint32_t nIndex) {
        if ((nIndex < m_data->beginOffset()) || (nIndex + 4 > m_data->endOffset()))
            UpdateBuffer(nIndex);
        return m_data->GetWord(nIndex);
    }

    inline uint16_t GetShortBE(uint32_t nIndex) {
        if ((nIndex < m_data->beginOffset()) || (nIndex + 2 > m_data->endOffset()))
            UpdateBuffer(nIndex);
        return m_data->GetShortBE(nIndex);
    }

    inline uint32_t GetWordBE(uint32_t nIndex) {
        if ((nIndex < m_data->beginOffset()) || (nIndex + 4 > m_data->endOffset()))
            UpdateBuffer(nIndex);
        return m_data->GetWordBE(nIndex);
    }

    inline bool IsValidOffset(uint32_t nIndex) { return (nIndex < fileSize); }

    inline void UseLoaders() { processFlags |= PF_USELOADERS; }
    inline void DontUseLoaders() { processFlags &= ~PF_USELOADERS; }
    inline void UseScanners() { processFlags |= PF_USESCANNERS; }
    inline void DontUseScanners() { processFlags &= ~PF_USESCANNERS; }

    uint32_t GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer);
    bool MatchBytes(const uint8_t *pattern, uint32_t nIndex, size_t nCount);
    bool MatchBytePattern(const BytePattern &pattern, uint32_t nIndex);
    bool SearchBytePattern(const BytePattern &pattern, uint32_t &nMatchOffset,
                           uint32_t nSearchOffset = 0, uint32_t nSearchSize = (uint32_t)-1);

    void AddContainedVGMFile(VGMFile *vgmfile);
    void RemoveContainedVGMFile(VGMFile *vgmfile);

    bool OnSaveAsRaw();

   public:
    DataSeg buf;
    uint32_t bufSize;

    float propreRatio;
    uint8_t processFlags;

   protected:
    std::ifstream file;
    std::filebuf *pbuf;
    bool bCanFileRead;
    unsigned long fileSize;
    std::wstring fullpath;
    std::wstring filename;
    std::wstring extension;
    std::wstring parRawFileFullPath;

    std::unique_ptr<DataBlock> m_data;

   public:
    std::list<VGMFile *> containedVGMFiles;
    VGMTag tag;
};

class VirtFile : public RawFile {
   public:
    VirtFile();
    VirtFile(uint8_t *data, uint32_t fileSize, const std::wstring &name,
             const wchar_t *parRawFileFullPath = L"", const VGMTag tag = VGMTag());
};