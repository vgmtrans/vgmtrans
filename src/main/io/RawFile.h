/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <string_view>
#include <filesystem>
#include <vector>
#include <climits>
#include <cassert>
#include <variant>
#include "mio.hpp"

#include "util/common.h"
#include "components/VGMTag.h"

class VGMFile;
class VGMItem;
class BytePattern;

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

class RawFile {
   public:
    virtual ~RawFile() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::filesystem::path path() const = 0;
    [[nodiscard]] virtual size_t size() const noexcept = 0;
    [[nodiscard]] virtual std::string stem() const noexcept = 0;
    [[nodiscard]] virtual std::string extension() const = 0;

    [[nodiscard]] bool isValidOffset(uint32_t ofs) const noexcept { return ofs < size(); }

    [[nodiscard]] bool useLoaders() const noexcept { return m_flags & UseLoaders; }
    void setUseLoaders(bool enable) noexcept {
        if (enable) {
            m_flags |= UseLoaders;
        } else {
            m_flags &= ~UseLoaders;
        }
    }
    [[nodiscard]] bool useScanners() const noexcept { return m_flags & UseScanners; }
    void setUseScanners(bool enable) noexcept {
        if (enable) {
            m_flags |= UseScanners;
        } else {
            m_flags &= ~UseScanners;
        }
    }

    template <typename T>
    [[nodiscard]] T get(const size_t ind) const {
        assert(ind + sizeof(T) <= size());

        T value = 0;
        for (size_t i = 0; i < sizeof(T); i++) {
            value |= (static_cast<u8>(operator[](ind + i)) << (i * CHAR_BIT));
        }

        return value;
    }

    template <typename T>
    [[nodiscard]] T getBE(const size_t ind) const {
        assert(ind + sizeof(T) <= size());

        T value = 0u;
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

    virtual const char &operator[](size_t i) const = 0;
    virtual uint8_t readByte(size_t offset) const = 0;
    virtual uint16_t readShort(size_t offset) const = 0;
    virtual uint32_t readWord(size_t offset) const = 0;
    virtual uint16_t readShortBE(size_t offset) const = 0;
    virtual uint32_t readWordBE(size_t offset) const = 0;
    std::string readNullTerminatedString(size_t offset, size_t maxLength) const;

    uint32_t readBytes(size_t offset, uint32_t nCount, void *pBuffer) const;
    bool matchBytes(const uint8_t *pattern, size_t offset, size_t nCount) const;
    bool matchBytePattern(const BytePattern &pattern, size_t offset) const;
    bool searchBytePattern(const BytePattern &pattern, uint32_t &nMatchOffset,
                           uint32_t nSearchOffset = 0, uint32_t nSearchSize = static_cast<uint32_t>(-1)) const;

    [[nodiscard]] const auto &containedVGMFiles() const noexcept {
        return m_vgmfiles;
    }
    void addContainedVGMFile(std::shared_ptr<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>>);
    void removeContainedVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>);

    VGMTag tag;

   private:
    std::vector<std::shared_ptr<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>>> m_vgmfiles;
    enum ProcessFlags { UseLoaders = 1, UseScanners = 2 };
    unsigned m_flags = UseLoaders | UseScanners;
};

class DiskFile final : public RawFile {
   public:
    DiskFile(const std::string &path);
    ~DiskFile() override = default;

    [[nodiscard]] std::string name() const override { return m_path.filename().string(); };
    [[nodiscard]] std::filesystem::path path() const override { return m_path; };
    [[nodiscard]] size_t size() const noexcept override { return m_data.length(); };
    [[nodiscard]] std::string stem() const noexcept override { return m_path.stem().string(); };
    [[nodiscard]] std::string extension() const override {
        auto tmp = m_path.extension().string();
        if (!tmp.empty()) {
            return toLower(tmp.substr(1, tmp.size() - 1));
        }

        return tmp;
    }

    const char *data() const override { return m_data.data(); }
    const char &operator[](size_t offset) const override { return m_data[offset]; }
    uint8_t readByte(size_t offset) const override { return m_data[offset]; }
    uint16_t readShort(size_t offset) const override { return get<u16>(offset); }
    uint32_t readWord(size_t offset) const override { return get<u32>(offset); }
    uint16_t readShortBE(size_t offset) const override { return getBE<u16>(offset); }
    uint32_t readWordBE(size_t offset) const override { return getBE<u32>(offset); }

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
             const VGMTag& tag = VGMTag());
    ~VirtFile() override = default;

    [[nodiscard]] std::string name() const override { return m_name; };
    [[nodiscard]] std::filesystem::path path() const override { return m_lpath; };
    [[nodiscard]] size_t size() const noexcept override {return m_data.size(); };
    [[nodiscard]] std::string stem() const noexcept override {
      auto tmp = m_lpath.stem();
      if (tmp.empty()) {
        std::filesystem::path tmp2(m_name);
        if (tmp2.has_filename()) {
          return tmp2.stem().string();
        }
      }

      return tmp.string();
    };
    [[nodiscard]] std::string extension() const override {
      std::filesystem::path tmp2(m_name);
      if (tmp2.has_extension()) {
        return toLower(tmp2.extension().string().substr(1, tmp2.extension().string().size() - 1));
      }
      auto tmp = m_lpath.extension().string();
      if (!tmp.empty()) {
        return toLower(tmp.substr(1, tmp.size() - 1));
      }
      return "";
    }

    const char *data() const override { return m_data.data(); }
    const char &operator[](size_t offset) const override { return m_data[offset]; }
    uint8_t readByte(size_t offset) const override { return m_data[offset]; }
    uint16_t readShort(size_t offset) const override { return get<u16>(offset); }
    uint32_t readWord(size_t offset) const override { return get<u32>(offset); }
    uint16_t readShortBE(size_t offset) const override { return getBE<u16>(offset); }
    uint32_t readWordBE(size_t offset) const override { return getBE<u32>(offset); }

   private:
    std::vector<char> m_data;
    std::string m_name;
    std::filesystem::path m_lpath;
};
