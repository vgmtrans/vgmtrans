/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <string_view>
#include <filesystem>
#include <vector>
#include "mio.hpp"

#include "common.h"
#include "VGMTag.h"

class VGMFile;
class VGMItem;
class BytePattern;

class RawFile {
   public:
    virtual ~RawFile() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string path() const = 0;
    [[nodiscard]] virtual size_t size() const noexcept = 0;
    [[nodiscard]] virtual std::string extension() const = 0;

    virtual std::string GetParRawFileFullPath() const { return {}; }

    bool IsValidOffset(uint32_t ofs) const noexcept { return ofs < size(); }

    bool useLoaders() const noexcept { return m_flags & UseLoaders; }
    void setUseLoaders(bool enable) noexcept {
        if (enable) {
            m_flags |= UseLoaders;
        } else {
            m_flags &= ~UseLoaders;
        }
    }
    bool useScanners() const noexcept { return m_flags & UseScanners; }
    void setUseScanners(bool enable) noexcept {
        if (enable) {
            m_flags |= UseScanners;
        } else {
            m_flags &= ~UseScanners;
        }
    }

    template <typename T>
    T get(const size_t ind) const {
        assert(ind + sizeof(T) <= size());

        T value = 0;
        for (size_t i = 0; i < sizeof(T); i++) {
            value |= (static_cast<u8>(operator[](ind + i)) << (i * CHAR_BIT));
        }

        return value;
    }

    template <typename T>
    T getBE(const size_t ind) const {
        assert(ind + sizeof(T) <= size());

        T value = 0;
        for (size_t i = 0; i < sizeof(T); i++) {
            value |= (static_cast<u8>(operator[](ind + i)) << ((sizeof(T) - i - 1) * CHAR_BIT));
        }

        return value;
    }

    const char *begin() const noexcept { return data(); }
    const char *end() const noexcept { return data() + size(); }
    std::reverse_iterator<const char *> rbegin() const noexcept {
        return std::reverse_iterator<const char *>(end());
    }
    std::reverse_iterator<const char *> rend() const noexcept {
        return std::reverse_iterator<const char *>(begin());
    }
    virtual const char *data() const = 0;

    virtual const char &operator[](const uint32_t i) const = 0;
    virtual uint8_t GetByte(uint32_t nIndex) const = 0;
    virtual uint16_t GetShort(uint32_t nIndex) const = 0;
    virtual uint32_t GetWord(uint32_t nIndex) const = 0;
    virtual uint16_t GetShortBE(uint32_t nIndex) const = 0;
    virtual uint32_t GetWordBE(uint32_t nIndex) const = 0;

    uint32_t GetBytes(uint32_t nIndex, uint32_t nCount, void *pBuffer) const;
    bool MatchBytes(const uint8_t *pattern, uint32_t nIndex, size_t nCount) const;
    bool MatchBytePattern(const BytePattern &pattern, uint32_t nIndex) const;
    bool SearchBytePattern(const BytePattern &pattern, uint32_t &nMatchOffset,
                           uint32_t nSearchOffset = 0, uint32_t nSearchSize = (uint32_t)-1) const;

    void AddContainedVGMFile(VGMFile *vgmfile);
    void RemoveContainedVGMFile(VGMFile *vgmfile);

    VGMItem *GetItemFromOffset(long offset);
    VGMFile *GetVGMFileFromOffset(long offset);

    std::list<VGMFile *> containedVGMFiles;
    VGMTag tag;

   private:
    enum ProcessFlags { UseLoaders = 1, UseScanners = 2 };
    int m_flags = UseLoaders | UseScanners;
};

class DiskFile final : public RawFile {
   public:
    DiskFile(const std::string &path);
    ~DiskFile() = default;

    [[nodiscard]] std::string name() const override { return m_path.filename().string(); };
    [[nodiscard]] std::string path() const override { return m_path.string(); };
    [[nodiscard]] size_t size() const noexcept override { return m_data.length(); };
    [[nodiscard]] std::string extension() const override {
        auto tmp = m_path.extension().string();
        if (!tmp.empty()) {
            return tmp.substr(1, tmp.size() - 1);
        }

        return tmp;
    }

    const char *data() const override { return m_data.data(); }
    const char &operator[](uint32_t offset) const override { return m_data[offset]; }
    uint8_t GetByte(uint32_t nIndex) const override { return m_data[nIndex]; }
    uint16_t GetShort(uint32_t nIndex) const override { return get<u16>(nIndex); }
    uint32_t GetWord(uint32_t nIndex) const override { return get<u32>(nIndex); }
    uint16_t GetShortBE(uint32_t nIndex) const override { return getBE<u16>(nIndex); }
    uint32_t GetWordBE(uint32_t nIndex) const override { return getBE<u32>(nIndex); }

   private:
    mio::mmap_source m_data;
    std::filesystem::path m_path;
};

class VirtFile final : public RawFile {
   public:
    VirtFile() = default;
    VirtFile(const RawFile &, size_t offset = 0);
    VirtFile(const RawFile &, size_t offset, size_t limit);
    VirtFile(const uint8_t *data, uint32_t size, std::string name, std::string parent_fullpath = "",
             const VGMTag tag = VGMTag());
    ~VirtFile() = default;

    [[nodiscard]] std::string name() const override { return m_name; };
    [[nodiscard]] std::string path() const override { return m_lpath.string(); };
    [[nodiscard]] size_t size() const noexcept override { return m_data.size(); };
    [[nodiscard]] std::string extension() const override {
        auto tmp = m_lpath.extension().string();
        if (!tmp.empty()) {
            return tmp.substr(1, tmp.size() - 1);
        } else {
            std::filesystem::path tmp2(m_name);
            if (tmp2.has_extension()) {
                return tmp2.extension().string().substr(1, tmp2.extension().string().size() - 1);
            }
        }

        return tmp;
    }

    std::string GetParRawFileFullPath() const override { return m_lpath.string(); }

    const char *data() const override { return m_data.data(); }
    const char &operator[](uint32_t offset) const override { return m_data[offset]; }
    uint8_t GetByte(uint32_t nIndex) const override { return m_data[nIndex]; }
    uint16_t GetShort(uint32_t nIndex) const override { return get<u16>(nIndex); }
    uint32_t GetWord(uint32_t nIndex) const override { return get<u32>(nIndex); }
    uint16_t GetShortBE(uint32_t nIndex) const override { return getBE<u16>(nIndex); }
    uint32_t GetWordBE(uint32_t nIndex) const override { return getBE<u32>(nIndex); }

   private:
    std::vector<char> m_data;
    std::string m_name;
    std::filesystem::path m_lpath;
};
