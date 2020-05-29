/*
 * VGMCis (c) 2002-2019
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

class PSFFile2 {
   public:
    explicit PSFFile2(const RawFile &file);
    ~PSFFile2() = default;

    [[nodiscard]] uint8_t version() const noexcept { return m_version; }
    [[nodiscard]] const std::map<std::string, std::string> &tags() const noexcept { return m_tags; }
    [[nodiscard]] const std::vector<char> &exe() const noexcept { return m_exe_data; }
    [[nodiscard]] const std::vector<char> &reservedSection() const noexcept { return m_reserved_data; }

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
