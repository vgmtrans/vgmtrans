/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include <cassert>
#include <climits>
#include <gsl-lite.hpp>

class RawFile;

constexpr auto PSF_TAG_SIG = "[TAG]";
constexpr auto PSF_TAG_SIG_LEN = 5;
constexpr auto PSF_STRIP_BUF_SIZE = 4096;

class PSFFile2 {
   public:
    PSFFile2(const RawFile &file);
    ~PSFFile2() = default;

    uint8_t version() const noexcept { return m_version; }
    const std::map<std::string, std::string> &tags() const noexcept { return m_tags; }
    const std::vector<char> &exe() const noexcept { return m_exe_data; }
    const std::vector<char> &reservedSection() const noexcept { return m_reserved_data; }

    template <typename T>
    T getExe(size_t ind) const {
        assert(ind + sizeof(T) < m_exe_data.size());

        T value = 0;
        for (size_t i = 0; i < sizeof(T); i++) {
            value |= (m_exe_data[ind + i] << (i * CHAR_BIT));
        }

        return value;
    }

    template <typename T>
    T getRes(size_t ind) const {
        assert(ind + sizeof(T) < m_reserved_data.size());

        T value = 0;
        for (size_t i = 0; i < sizeof(T); i++) {
            value |= (m_reserved_data[ind + i] << (i * CHAR_BIT));
        }

        return value;
    }

   private:
    uint8_t m_version;
    uint32_t m_exe_CRC;
    std::vector<char> m_exe_data;
    std::vector<char> m_reserved_data;
    std::map<std::string, std::string> m_tags;

    void parseTags(gsl::span<const char> data);
};
