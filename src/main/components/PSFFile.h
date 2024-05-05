/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <span>
#include <string>
#include <cassert>
#include <climits>

class RawFile;

class PSFFile {
   public:
    explicit PSFFile(const RawFile &file);
    ~PSFFile() = default;

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

    void parseTags(std::span<const char> data);
};
